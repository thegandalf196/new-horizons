/*
 * NewHorizonsLeadershipAdmissionTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <tuple>

#include "../../lib/GameConstants.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
class LeadershipRecordingServer final : public IGameServer
{
public:
	explicit LeadershipRecordingServer(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	void setState(EServerState value) override { serverState = value; }
	EServerState getState() const override { return serverState; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor player, GameConnectionID connection) const override
	{
		return player == PlayerColor(0) && connection == GameConnectionID::FIRST_CONNECTION;
	}
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return false; }

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * ended = dynamic_cast<PlayerEndsGame *>(&pack))
			playerEndsGamePacks.push_back(*ended);
		if(const auto * ended = dynamic_cast<PlayerEndsTurn *>(&pack))
			playerEndsTurnPacks.push_back(*ended);
		if(const auto * dialog = dynamic_cast<BlockingDialog *>(&pack))
			blockingDialogQuery = dialog->queryID;
		if(const auto * dialog = dynamic_cast<GarrisonDialog *>(&pack))
		{
			++garrisonDialogs;
			garrisonDialogQuery = dialog->queryID;
			garrisonObject = dialog->objid;
			garrisonHero = dialog->hid;
		}
		if(const auto * message = dynamic_cast<SystemMessage *>(&pack))
		{
			++systemMessages;
			systemMessageTexts.push_back(message->text.toString(LIBRARY->generaltexth.get()));
		}
		if(dynamic_cast<RebalanceStacks *>(&pack))
			++rebalancePacks;
		state->apply(pack);
	}

	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(const auto * applied = dynamic_cast<const PackageApplied *>(&pack))
			responses.push_back(*applied);
		if(const auto * message = dynamic_cast<const SystemMessage *>(&pack))
			targetedSystemMessageTexts.push_back(message->text.toString(LIBRARY->generaltexth.get()));
	}

	std::vector<PackageApplied> responses;
	std::vector<PlayerEndsGame> playerEndsGamePacks;
	std::vector<PlayerEndsTurn> playerEndsTurnPacks;
	std::vector<std::string> systemMessageTexts;
	std::vector<std::string> targetedSystemMessageTexts;
	QueryID blockingDialogQuery = QueryID::NONE;
	QueryID garrisonDialogQuery = QueryID::NONE;
	ObjectInstanceID garrisonObject = ObjectInstanceID::NONE;
	ObjectInstanceID garrisonHero = ObjectInstanceID::NONE;
	int systemMessages = 0;
	int rebalancePacks = 0;
	int garrisonDialogs = 0;

	bool hasLeadershipLimitMessage() const
	{
		const auto isLeadershipLimitMessage = [](const std::string & text)
		{
			return text.find("Leadership limit exceeded") != std::string::npos;
		};
		return std::any_of(systemMessageTexts.begin(), systemMessageTexts.end(), isLeadershipLimitMessage)
			|| std::any_of(targetedSystemMessageTexts.begin(), targetedSystemMessageTexts.end(), isLeadershipLimitMessage);
	}

private:
	EServerState serverState = EServerState::GAMEPLAY;
	std::shared_ptr<CGameState> state;
};

class NewHorizonsLeadershipAdmissionTest : public TinyMapGameTest
{
protected:
    void SetUp() override
    {
        TinyMapGameTest::SetUp();
        if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
            GTEST_SKIP() << "Requires the New Horizons module";
    }

    void mapLoaded(CMap * loaded) override
    {
        TinyMapGameTest::mapLoaded(loaded);
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
            JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
        auto capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
        // The reported runtime snapshot assigned Halflings a 60-Leadership
        // requirement; keep that observed value local to this regression.
        capabilities["leadership"]["creatureRequirements"]["core:halfling"] = JsonNode(60);
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
    }

    static HeroTypeID heroType(const char * id)
    {
        return HeroTypeID(HeroTypeID::decode(id));
    }

    static FactionID faction(const char * id)
    {
        return FactionID(FactionID::decode(id));
    }
};

class LegacyLeadershipAdmissionTest : public NewHorizonsLeadershipAdmissionTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsLeadershipAdmissionTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
	}
};
}

TEST_F(NewHorizonsLeadershipAdmissionTest, StrongholdBlacksmithUsesSavedOffersAndBallistaYardRefreshesSiege)
{
	const PlayerColor player(0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town({12, 12, 0}, FactionID::STRONGHOLD, player)
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.heroGarrison({{CreatureID(0), 1}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(player);
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(town, nullptr);
	town->addBuilding(BuildingID::BLACKSMITH);
	town->addBuilding(BuildingID::SPECIAL_3);

	const auto offers = town->getWarMachineShopOffers();
	ASSERT_EQ(offers.size(), 3u);
	EXPECT_EQ(offers[0].artifact, ArtifactID::BALLISTA);
	EXPECT_EQ(offers[1].artifact, ArtifactID::AMMO_CART);
	EXPECT_EQ(offers[2].artifact, ArtifactID::FIRST_AID_TENT);
	EXPECT_FALSE(std::any_of(offers.begin(), offers.end(),
		[](const auto & offer) { return offer.artifact == ArtifactID::CATAPULT; }));

	const int ordinaryAmmoCartPrice = ArtifactID(ArtifactID::AMMO_CART).toArtifact()->getPrice();
	const int favoredPrice = static_cast<int>(((static_cast<int64_t>(ordinaryAmmoCartPrice) * 75 + 2500) / 5000) * 50);
	EXPECT_EQ(offers[1].price, favoredPrice);

	town->setVisitingHero(hero);
	grantResources(player, GameResID(EGameResID::GOLD), 10000);
	const auto goldAvailable = gameState()->getPlayerState(player)->resources[EGameResID::GOLD];
	GameHandlerTestServer server(gameState(), player);
	CGameHandler gameHandler(server, gameState());
	EXPECT_FALSE(gameHandler.buyArtifact(hero->id, ArtifactID::CATAPULT));
	EXPECT_EQ(gameState()->getPlayerState(player)->resources[EGameResID::GOLD], goldAvailable);
	ASSERT_TRUE(gameHandler.buyArtifact(hero->id, ArtifactID::AMMO_CART));
	EXPECT_TRUE(hero->hasArt(ArtifactID::AMMO_CART));
	EXPECT_EQ(gameState()->getPlayerState(player)->resources[EGameResID::GOLD], goldAvailable - favoredPrice);

	hero->pos = town->visitablePos() - hero->getVisitableOffset();
	town->onHeroVisit(gameHandler, hero);
	auto countSiegeBonuses = [hero]()
	{
		return std::count_if(hero->getExportedBonusList().begin(), hero->getExportedBonusList().end(),
			[](const auto & bonus) { return bonus->type == BonusType::SIEGE_RATING; });
	};
	ASSERT_EQ(countSiegeBonuses(), 1);
	ASSERT_TRUE(hero->getSiegeCapabilities());
	EXPECT_EQ(hero->getSiegeCapabilities()->siegeRating, 20);
	auto bonus = *std::find_if(hero->getExportedBonusList().begin(), hero->getExportedBonusList().end(),
		[](const auto & candidate) { return candidate->type == BonusType::SIEGE_RATING; });
	const auto calendar = gameState()->getCalendar();
	const int remainingDays = calendar.getDaysInWeek() + 1 - calendar.getDayOfWeek();
	bonus->turnsRemain = remainingDays - 1;
	town->onHeroVisit(gameHandler, hero);
	EXPECT_EQ(countSiegeBonuses(), 1);
	auto refreshedBonus = *std::find_if(hero->getExportedBonusList().begin(), hero->getExportedBonusList().end(),
		[](const auto & candidate) { return candidate->type == BonusType::SIEGE_RATING; });
	EXPECT_EQ(refreshedBonus->val, 20);
	EXPECT_EQ(refreshedBonus->turnsRemain, remainingDays);
	EXPECT_EQ(hero->getSiegeCapabilities()->siegeRating, 20);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, JoiningArmyClampsDuplicateStacksAndLeavesFullArmyRemainder)
{
    const CreatureID pikeman(CreatureID::decode("core:pikeman"));
    const CreatureID archer(CreatureID::decode("core:archer"));
    const CreatureID swordsman(CreatureID::decode("core:swordsman"));
    const CreatureID griffin(CreatureID::decode("core:griffin"));
    const CreatureID monk(CreatureID::decode("core:monk"));
    const CreatureID cavalier(CreatureID::decode("core:cavalier"));
    const CreatureID angel(CreatureID::decode("core:angel"));
    const CreatureID centaur(CreatureID::decode("core:centaur"));

    TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
    builder.size(48, false).playerActive(PlayerColor(0))
        .town({12, 12, 0}, faction("core:castle"), PlayerColor(0))
        // Duplicate incoming types are legal in an armed source.  The archer
        // is deliberately absent from the full destination army, exercising
        // the manual garrison fallback as well.
        .townGarrison({{pikeman, 3}, {pikeman, 4}, {archer, 2}})
        .hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
        .heroGarrison({
            {pikeman, 16}, {swordsman, 1}, {griffin, 1}, {monk, 1},
            {cavalier, 1}, {angel, 1}, {centaur, 1}
        });
    startWithMap(std::move(builder));

    auto * hero = findHeroByOwner(PlayerColor(0));
    auto * source = findFirst<CGTownInstance>();
    ASSERT_NE(hero, nullptr);
    ASSERT_NE(source, nullptr);

    const auto pikemanCapacity = hero->getLeadershipSlotCapacity(pikeman);
    ASSERT_TRUE(pikemanCapacity);
    ASSERT_EQ(pikemanCapacity->maximum, 17);
    ASSERT_EQ(hero->stacksCount(), GameConstants::ARMY_SIZE);
    ASSERT_EQ(source->stacksCount(), 3);

    GameHandlerTestServer server(gameState(), PlayerColor(0));
    CGameHandler gameHandler(server, gameState());

    // A full-count transfer would exceed the existing stack's Leadership
    // limit.  The authoritative join path must take only the one legal unit,
    // carry that clamp across the duplicate second pikeman stack, and leave
    // the unmatched archer for the garrison fallback.
    gameHandler.tryJoiningArmy(source, hero, false, true);

    EXPECT_EQ(hero->getStackCount(SlotID(0)), 17);
    EXPECT_EQ(source->getStackCount(SlotID(0)), 2);
    EXPECT_EQ(source->getStackCount(SlotID(1)), 4);
    EXPECT_EQ(source->getStackCount(SlotID(2)), 2);
    EXPECT_EQ(hero->stacksCount(), GameConstants::ARMY_SIZE);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, FreeTierOneDwellingKeepsUnitsAboveRemainingCapacity)
{
    TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
    builder.size(36, false).playerActive(PlayerColor(0))
        // CREATURE_GENERATOR1 subtype 56 is the core Pikeman dwelling, a
        // guaranteed tier-one source for the free recruitment path.
        .dwelling({12, 12, 0}, MapObjectSubID(56), PlayerColor(0))
        .hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
    startWithMap(std::move(builder));

    auto * hero = findHeroByOwner(PlayerColor(0));
    auto * dwelling = findFirst<CGDwelling>();
    ASSERT_NE(hero, nullptr);
    ASSERT_NE(dwelling, nullptr);
    ASSERT_FALSE(dwelling->creatures.empty());
    ASSERT_FALSE(dwelling->creatures.front().second.empty());

    const CreatureID creature = dwelling->creatures.front().second.front();
    ASSERT_EQ(creature.toCreature()->getLevel(), 1);
    const auto capacity = hero->getLeadershipSlotCapacity(creature);
    ASSERT_TRUE(capacity);

    hero->clearSlots();
    ASSERT_TRUE(hero->setCreature(SlotID(0), creature, capacity->maximum - 1));
    dwelling->creatures.front().first = 3;

    GameHandlerTestServer server(gameState(), PlayerColor(0));
    CGameHandler gameHandler(server, gameState());
    static_cast<const IObjectInterface *>(dwelling)->blockingDialogAnswered(gameHandler, hero, 1);

    EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
    EXPECT_EQ(dwelling->creatures.front().first, 2u);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, AcceptedWanderingFollowersKeepLeadershipRemainderInGarrison)
{
	const CreatureID halfling(CreatureID::decode("core:halfling"));
	const PlayerColor player(0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.monster({12, 12, 0}, halfling, 2, static_cast<int8_t>(CGCreature::Character::COMPLIANT));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(player);
	auto * wanderingFollowers = findFirst<CGCreature>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(wanderingFollowers, nullptr);
	const ObjectInstanceID wanderingFollowersId = wanderingFollowers->id;
	const auto capacity = hero->getLeadershipSlotCapacity(halfling);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 0);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), halfling, capacity->maximum));
	ASSERT_EQ(wanderingFollowers->getStackCount(SlotID(0)), 2);

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);

	// Follow the actual visit/query path. Resolving the Followers offer calls
	// CGCreature::blockingDialogAnswered and then the authoritative join handler.
	gameHandler.objectVisited(wanderingFollowers, hero);
	ASSERT_NE(server.blockingDialogQuery, QueryID::NONE);
	ASSERT_TRUE(gameHandler.queryReply(server.blockingDialogQuery, 1, player));

	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(wanderingFollowers->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + wanderingFollowers->getStackCount(SlotID(0)), capacity->maximum + 2);
	EXPECT_EQ(server.garrisonDialogs, 1);
	EXPECT_EQ(server.garrisonObject, wanderingFollowers->id);
	EXPECT_EQ(server.garrisonHero, hero->id);
	EXPECT_TRUE(server.garrisonDialogQuery != QueryID::NONE);
	EXPECT_FALSE(server.hasLeadershipLimitMessage());

	// The garrison exchange can preserve the excess as a second legal stack
	// instead of attempting to merge it beyond the full first stack's limit.
	ArrangeStacks retainRemainder(1, SlotID(1), SlotID(0), hero->id, wanderingFollowers->id, 0);
	retainRemainder.player = player;
	retainRemainder.requestID = 71;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, retainRemainder);
	ASSERT_FALSE(server.responses.empty());
	ASSERT_EQ(server.responses.back().requestID, retainRemainder.requestID);
	ASSERT_TRUE(server.responses.back().result);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	EXPECT_TRUE(wanderingFollowers->slotEmpty(SlotID(0)));
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + hero->getStackCount(SlotID(1)), capacity->maximum + 2);
	EXPECT_FALSE(server.hasLeadershipLimitMessage());

	EXPECT_TRUE(gameHandler.queryReply(server.garrisonDialogQuery, 0, player));
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + hero->getStackCount(SlotID(1)), capacity->maximum + 2);
	EXPECT_EQ(gameState()->getObjInstance(wanderingFollowersId), nullptr);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, AcceptedWanderingFollowersUseOneFreeLeadershipSlotAndKeepRemainder)
{
	const CreatureID halfling(CreatureID::decode("core:halfling"));
	const PlayerColor player(0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.monster({12, 12, 0}, halfling, 2, static_cast<int8_t>(CGCreature::Character::COMPLIANT));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(player);
	auto * wanderingFollowers = findFirst<CGCreature>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(wanderingFollowers, nullptr);
	const ObjectInstanceID wanderingFollowersId = wanderingFollowers->id;
	const auto capacity = hero->getLeadershipSlotCapacity(halfling);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), halfling, capacity->maximum - 1));
	ASSERT_EQ(wanderingFollowers->getStackCount(SlotID(0)), 2);
	const TQuantity initialArmyCount = capacity->maximum + 1;

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	gameHandler.objectVisited(wanderingFollowers, hero);
	ASSERT_NE(server.blockingDialogQuery, QueryID::NONE);
	ASSERT_TRUE(gameHandler.queryReply(server.blockingDialogQuery, 1, player));

	ASSERT_EQ(gameState()->getObjInstance(wanderingFollowersId), wanderingFollowers);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(wanderingFollowers->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + wanderingFollowers->getStackCount(SlotID(0)), initialArmyCount);
	EXPECT_EQ(server.garrisonDialogs, 1);
	EXPECT_EQ(server.garrisonObject, wanderingFollowersId);
	EXPECT_EQ(server.garrisonHero, hero->id);
	EXPECT_TRUE(server.garrisonDialogQuery != QueryID::NONE);
	EXPECT_FALSE(server.hasLeadershipLimitMessage());
}

TEST_F(NewHorizonsLeadershipAdmissionTest, AcceptedWanderingFollowersSingleRemainderCanSwapIntoEmptyHeroSlot)
{
	const CreatureID halfling(CreatureID::decode("core:halfling"));
	const PlayerColor player(0);
	const PlayerColor opponent(1);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.playerActive(opponent)
		.hero({20, 20, 0}, heroType("core:valeska"), opponent)
		.monster({12, 12, 0}, halfling, 2, static_cast<int8_t>(CGCreature::Character::COMPLIANT));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(player);
	auto * wanderingFollowers = findFirst<CGCreature>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(wanderingFollowers, nullptr);
	const auto capacity = hero->getLeadershipSlotCapacity(halfling);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), halfling, capacity->maximum - 1));
	ASSERT_EQ(wanderingFollowers->getStackCount(SlotID(0)), 2);

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	ASSERT_TRUE(vstd::contains(gameState()->actingPlayers, player));
	gameHandler.objectVisited(wanderingFollowers, hero);
	EXPECT_TRUE(vstd::contains(gameState()->actingPlayers, player)) << "object visit unexpectedly ended the player's active turn";
	ASSERT_NE(server.blockingDialogQuery, QueryID::NONE);
	ASSERT_TRUE(gameHandler.queryReply(server.blockingDialogQuery, 1, player));
	EXPECT_TRUE(vstd::contains(gameState()->actingPlayers, player)) << "accepted offer unexpectedly ended the player's active turn";
	EXPECT_TRUE(server.playerEndsGamePacks.empty());
	EXPECT_TRUE(server.playerEndsTurnPacks.empty());

	ASSERT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	ASSERT_EQ(wanderingFollowers->getStackCount(SlotID(0)), 1);
	auto topQuery = gameHandler.queries->topQuery(player);
	ASSERT_NE(topQuery, nullptr);
	ASSERT_EQ(topQuery->getType(), QueryType::GarrisonDialog);
	ASSERT_EQ(server.garrisonObject, wanderingFollowers->id);
	ASSERT_EQ(server.garrisonHero, hero->id);

	const size_t systemMessageCountBeforeSwap = server.systemMessageTexts.size();
	const size_t targetedMessageCountBeforeSwap = server.targetedSystemMessageTexts.size();
	ArrangeStacks moveLastRemainder(1, SlotID(1), SlotID(0), hero->id, wanderingFollowers->id, 0);
	moveLastRemainder.player = player;
	moveLastRemainder.requestID = 72;
	ASSERT_TRUE(vstd::contains(gameState()->actingPlayers, player));
	ASSERT_FALSE(topQuery->blocksPack(&moveLastRemainder));
	ASSERT_FALSE(gameHandler.isBlockedByQueries(&moveLastRemainder, player));
	ASSERT_TRUE(gameHandler.isAllowedExchange(hero->id, wanderingFollowers->id));
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, moveLastRemainder);

	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_EQ(server.responses.back().requestID, moveLastRemainder.requestID);
	const bool packageApplied = server.responses.back().result;
	EXPECT_TRUE(packageApplied);
	EXPECT_EQ(server.systemMessageTexts.size(), systemMessageCountBeforeSwap);
	EXPECT_EQ(server.targetedSystemMessageTexts.size(), targetedMessageCountBeforeSwap);
	EXPECT_FALSE(server.hasLeadershipLimitMessage());
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + hero->getStackCount(SlotID(1)), capacity->maximum + 1);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 1);
	EXPECT_TRUE(wanderingFollowers->slotEmpty(SlotID(0)));
}

TEST_F(NewHorizonsLeadershipAdmissionTest, OrdinaryMergeClampsToPerSlotLeadershipAndLeavesTheRemainder)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{pikeman, 17}, {pikeman, 2}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto capacity = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	ASSERT_EQ(hero->getStackCount(SlotID(1)), 2);

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));

	// No room means no partial mutation and an ordinary combine is rejected.
	ArrangeStacks noFit(2, SlotID(1), SlotID(0), hero->id, hero->id, 0);
	noFit.player = PlayerColor(0);
	noFit.requestID = 41;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, noFit);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 1);
	ASSERT_EQ(server.systemMessageTexts.size(), 1u);
	const std::string expectedMessage = "Leadership limit exceeded: this hero can command at most "
		+ std::to_string(capacity->maximum) + " creatures of this type ("
		+ std::to_string(capacity->requirement) + " Leadership each; hero Leadership "
		+ std::to_string(capacity->leadership) + ").";
	EXPECT_NE(server.systemMessageTexts.back().find(expectedMessage), std::string::npos);

	// One legal unit is transferred while the excess stays in its source stack.
	hero->setStackCount(SlotID(0), capacity->maximum - 1);
	server.responses.clear();
	server.systemMessages = 0;
	ArrangeStacks partial(2, SlotID(1), SlotID(0), hero->id, hero->id, 0);
	partial.player = PlayerColor(0);
	partial.requestID = 42;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, partial);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 1);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);

	// Exact fit consumes the complete source stack.
	hero->setStackCount(SlotID(0), capacity->maximum - 2);
	hero->setStackCount(SlotID(1), 2);
	server.responses.clear();
	ArrangeStacks exactFit(2, SlotID(1), SlotID(0), hero->id, hero->id, 0);
	exactFit.player = PlayerColor(0);
	exactFit.requestID = 43;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, exactFit);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_FALSE(hero->hasStackAtSlot(SlotID(1)));
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, WholeStackDragIntoEmptyHeroSlotFillsLeadershipCapacityAndLeavesRemainder)
{
	const CreatureID dendroidGuard(CreatureID::decode("core:dendroidGuard"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.town({12, 12, 0}, faction("core:rampart"), PlayerColor(0))
		.townGarrison({{dendroidGuard, 25}})
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	auto * garrison = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(garrison, nullptr);
	garrison->setVisitingHero(hero);
	hero->clearSlots();
	ASSERT_TRUE(garrison->hasStackAtSlot(SlotID(0)));
	ASSERT_FALSE(hero->hasStackAtSlot(SlotID(0)));
	const auto capacity = hero->getLeadershipSlotCapacity(dendroidGuard);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 0);
	ASSERT_LT(capacity->maximum, garrison->getStackCount(SlotID(0)));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));
	// The exchange UI sends the clicked empty hero destination first and the
	// previously selected garrison source second.
	ArrangeStacks drag(1, SlotID(0), SlotID(0), hero->id, garrison->id, 0);
	drag.player = PlayerColor(0);
	drag.requestID = 49;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, drag);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(garrison->getStackCount(SlotID(0)), 25 - capacity->maximum);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, WholeStackDragRejectsTwoEmptySlotsWithoutMutation)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.town({12, 12, 0}, faction("core:rampart"), PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	auto * garrison = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(garrison, nullptr);
	garrison->setVisitingHero(hero);
	hero->clearSlots();
	ASSERT_TRUE(hero->slotEmpty(SlotID(0)));
	ASSERT_TRUE(garrison->slotEmpty(SlotID(0)));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));
	// Empty hero destination first exercises the same branch as the reported UI
	// route while proving a missing garrison source is rejected safely.
	ArrangeStacks emptyDrag(1, SlotID(0), SlotID(0), hero->id, garrison->id, 0);
	emptyDrag.player = PlayerColor(0);
	emptyDrag.requestID = 51;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, emptyDrag);

	EXPECT_TRUE(hero->slotEmpty(SlotID(0)));
	EXPECT_TRUE(garrison->slotEmpty(SlotID(0)));
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 1);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, LastHeroCreatureDragToEmptyGarrisonReportsGameplayReason)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const PlayerColor player(0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town({12, 12, 0}, faction("core:castle"), player)
		.hero({5, 5, 0}, heroType("core:christian"), player);
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(player);
	auto * garrison = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(garrison, nullptr);
	garrison->setVisitingHero(hero);
	hero->clearSlots();
	ASSERT_TRUE(hero->needsLastStack());
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, 1));
	ASSERT_TRUE(garrison->slotEmpty(SlotID(0)));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	// Match the ordinary whole-stack drag intent with the empty garrison target
	// first. The server must reject this without receiving a zero-count split.
	ArrangeStacks drag(1, SlotID(0), SlotID(0), garrison->id, hero->id, 0);
	drag.player = player;
	drag.requestID = 52;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, drag);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
	EXPECT_TRUE(garrison->slotEmpty(SlotID(0)));
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + garrison->getStackCount(SlotID(0)), 1);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	ASSERT_EQ(server.systemMessageTexts.size(), 1u);
	EXPECT_NE(server.systemMessageTexts.back().find("Cannot move away the last creature!"), std::string::npos);
	EXPECT_EQ(server.systemMessageTexts.back().find("No creatures to split"), std::string::npos);
	EXPECT_EQ(server.rebalancePacks, 0);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, LastHeroStackDragToEmptyGarrisonTransfersAllButOne)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const PlayerColor player(0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town({12, 12, 0}, faction("core:castle"), player)
		.hero({5, 5, 0}, heroType("core:christian"), player);
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(player);
	auto * garrison = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(garrison, nullptr);
	garrison->setVisitingHero(hero);
	hero->clearSlots();
	ASSERT_TRUE(hero->needsLastStack());
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, 4));
	ASSERT_TRUE(garrison->slotEmpty(SlotID(0)));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	ArrangeStacks drag(1, SlotID(0), SlotID(0), garrison->id, hero->id, 0);
	drag.player = player;
	drag.requestID = 55;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, drag);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(garrison->getStackCount(SlotID(0)), 3);
	EXPECT_EQ(hero->getStackCount(SlotID(0)) + garrison->getStackCount(SlotID(0)), 4);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
	EXPECT_EQ(server.rebalancePacks, 1);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, LastHeroStackMoveToEmptyHeroSlotTransfersMaximumLegalAmountAndKeepsOne)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const PlayerColor player(0);
	const int3 townPosition(12, 12, 0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town(townPosition, faction("core:castle"), player)
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.hero({6, 6, 0}, heroType("core:valeska"), player);
	startWithMap(std::move(builder));

	auto * town = findFirst<CGTownInstance>();
	auto heroes = findAll<CGHeroInstance>();
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(heroes.size(), 2u);
	auto * destination = heroes[0];
	auto * source = heroes[1];
	destination->clearSlots();
	source->clearSlots();
	ASSERT_TRUE(source->needsLastStack());
	SetHeroesInTown setHeroes;
	setHeroes.tid = town->id;
	setHeroes.visiting = destination->id;
	setHeroes.garrison = source->id;
	gameState()->apply(setHeroes);

	const auto capacity = destination->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	// Leave more than one creature if the receiving hero's capacity, rather
	// than the last-stack rule, is the limiting factor.
	const TQuantity sourceCount = capacity->maximum + 5;
	ASSERT_TRUE(source->setCreature(SlotID(0), pikeman, sourceCount));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	// As in the exchange UI, send the clicked empty destination before the
	// selected source. The server reserves one creature and applies Leadership.
	ArrangeStacks drag(1, SlotID(0), SlotID(0), destination->id, source->id, 0);
	drag.player = player;
	drag.requestID = 53;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, drag);

	EXPECT_EQ(destination->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(source->getStackCount(SlotID(0)), 5);
	EXPECT_EQ(destination->getStackCount(SlotID(0)) + source->getStackCount(SlotID(0)), sourceCount);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
	EXPECT_EQ(server.rebalancePacks, 1);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, StaleLastStackMoveToEmptyGarrisonDoesNotMutate)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const PlayerColor player(0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town({12, 12, 0}, faction("core:castle"), player)
		.hero({5, 5, 0}, heroType("core:christian"), player);
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(player);
	auto * garrison = findFirst<CGTownInstance>();
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(garrison, nullptr);
	garrison->setVisitingHero(hero);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, 2));
	const TQuantity expectedGarrisonCount = garrison->getStackCount(SlotID(0));
	hero->eraseStack(SlotID(0)); // The source vanished after the drag request was built.

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	ArrangeStacks stale(1, SlotID(0), SlotID(0), garrison->id, hero->id, 0);
	stale.player = player;
	stale.requestID = 54;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, stale);

	EXPECT_TRUE(hero->slotEmpty(SlotID(0)));
	EXPECT_EQ(garrison->getStackCount(SlotID(0)), expectedGarrisonCount);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	ASSERT_EQ(server.systemMessageTexts.size(), 1u);
	EXPECT_NE(server.systemMessageTexts.back().find("No stack to move!"), std::string::npos);
	EXPECT_EQ(server.systemMessageTexts.back().find("No creatures to split"), std::string::npos);
	EXPECT_EQ(server.rebalancePacks, 0);
}

TEST_F(LegacyLeadershipAdmissionTest, OrdinaryMergeRemainsUncappedWithoutSavedLeadershipRules)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{pikeman, 1000}, {pikeman, 2}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getLeadershipSlotCapacity(pikeman));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));
	ArrangeStacks merge(2, SlotID(1), SlotID(0), hero->id, hero->id, 0);
	merge.player = PlayerColor(0);
	merge.requestID = 50;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, merge);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1002);
	EXPECT_FALSE(hero->hasStackAtSlot(SlotID(1)));
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, NumericSplitRejectsNegativeDeltaAndOverCapacityButAcceptsNoOp)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{pikeman, 17}, {pikeman, 2}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto capacity = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));

	// An explicit numeric request that lowers the destination's current count
	// is malformed/stale, not a reverse transfer.
	ArrangeStacks negativeDelta(3, SlotID(1), SlotID(0), hero->id, hero->id, capacity->maximum - 1);
	negativeDelta.player = PlayerColor(0);
	negativeDelta.requestID = 44;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, negativeDelta);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);

	// A zero-delta request is a successful no-op and emits no state pack.
	server.responses.clear();
	server.systemMessages = 0;
	ArrangeStacks zeroDelta(3, SlotID(1), SlotID(0), hero->id, hero->id, capacity->maximum);
	zeroDelta.player = PlayerColor(0);
	zeroDelta.requestID = 45;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, zeroDelta);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);
	EXPECT_EQ(server.systemMessages, 0);

	// Numeric split requests remain exact: do not silently clamp a request of
	// two units to the one unit that fits in the receiving stack.
	hero->setStackCount(SlotID(0), capacity->maximum - 1);
	server.responses.clear();
	server.systemMessages = 0;
	ArrangeStacks overCapacity(3, SlotID(1), SlotID(0), hero->id, hero->id, capacity->maximum + 1);
	overCapacity.player = PlayerColor(0);
	overCapacity.requestID = 46;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, overCapacity);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum - 1);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);
	EXPECT_EQ(server.systemMessages, 1);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, StaleNumericSplitDoesNotMutateArmy)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{pikeman, 10}, {pikeman, 2}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const TQuantity destinationBefore = hero->getStackCount(SlotID(0));
	hero->eraseStack(SlotID(1)); // the client request was built before this state change

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));
	ArrangeStacks stale(3, SlotID(1), SlotID(0), hero->id, hero->id, destinationBefore + 1);
	stale.player = PlayerColor(0);
	stale.requestID = 47;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, stale);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), destinationBefore);
	EXPECT_FALSE(hero->hasStackAtSlot(SlotID(1)));
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.rebalancePacks, 0);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, CrossHeroMergeKeepsRequiredLastCreatureAndTransfersAvailableCapacity)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const PlayerColor player(0);
	const int3 townPosition(12, 12, 0);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town(townPosition, faction("core:castle"), player)
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.hero({6, 6, 0}, heroType("core:valeska"), player);
	startWithMap(std::move(builder));

	auto * town = findFirst<CGTownInstance>();
	auto heroes = findAll<CGHeroInstance>();
	ASSERT_NE(town, nullptr);
	ASSERT_EQ(heroes.size(), 2u);
	auto * destination = heroes[0];
	auto * source = heroes[1];
	destination->clearSlots();
	source->clearSlots();
	const auto capacity = destination->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	ASSERT_TRUE(destination->setCreature(SlotID(0), pikeman, capacity->maximum - 1));
	ASSERT_TRUE(source->setCreature(SlotID(0), pikeman, 2));
	SetHeroesInTown setHeroes;
	setHeroes.tid = town->id;
	setHeroes.visiting = destination->id;
	setHeroes.garrison = source->id;
	gameState()->apply(setHeroes);

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);
	ArrangeStacks merge(2, SlotID(0), SlotID(0), source->id, destination->id, 0);
	merge.player = player;
	merge.requestID = 48;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, merge);

	EXPECT_EQ(destination->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(source->getStackCount(SlotID(0)), 1);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, BulkMergeTransfersOnlyAvailableLeadershipCapacity)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto capacity = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	hero->clearSlots();
	ASSERT_TRUE(hero->setCreature(SlotID(0), pikeman, capacity->maximum - 1));
	ASSERT_TRUE(hero->setCreature(SlotID(1), pikeman, 2));
	ASSERT_TRUE(hero->setCreature(SlotID(2), pikeman, 3));

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));
	BulkMergeStacks merge(hero->id, SlotID(0));
	merge.player = PlayerColor(0);
	merge.requestID = 49;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, merge);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 1);
	EXPECT_EQ(hero->getStackCount(SlotID(2)), 3);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, GarrisonSwapRejectsAnOversizedTownStackAtomically)
{
	const CreatureID imp(CreatureID::decode("core:imp"));
	const CreatureID gog(CreatureID::decode("core:gog"));
	const PlayerColor player(0);
	const int3 townPosition(12, 12, 0);

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(player)
		.town(townPosition, faction("core:inferno"), player)
		.townGarrison({{imp, 1}, {gog, 1}})
		.hero({5, 5, 0}, heroType("core:christian"), player)
		.heroGarrison({});
	startWithMap(std::move(builder));

	auto * town = findFirst<CGTownInstance>();
	auto * hero = findHeroByOwner(player);
	ASSERT_NE(town, nullptr);
	ASSERT_NE(hero, nullptr);

	// This is the state that makes Nullkiller consider moving the visiting hero
	// into the empty garrison.  Keep the town army above the hero's per-stack
	// Leadership limit so the authoritative moveArmy preflight must reject it.
	ChangeObjPos moveHero;
	moveHero.objid = hero->id;
	moveHero.nPos = town->visitablePos();
	moveHero.initiator = player;
	gameState()->apply(moveHero);
	SetHeroesInTown setHeroes;
	setHeroes.tid = town->id;
	setHeroes.visiting = hero->id;
	setHeroes.garrison = ObjectInstanceID::NONE;
	gameState()->apply(setHeroes);

	const auto capacity = hero->getLeadershipSlotCapacity(gog);
	ASSERT_TRUE(capacity);
	town->setStackCount(SlotID(1), capacity->maximum + 1);
	const auto snapshotArmy = [](const CArmedInstance & army)
	{
		std::vector<std::tuple<int, int, TQuantity>> snapshot;
		for(const auto & [slot, stack] : army.Slots())
			snapshot.emplace_back(slot.getNum(), stack->getCreatureID().getNum(), stack->getCount());
		return snapshot;
	};
	const auto townArmyBefore = snapshotArmy(*town);
	const auto heroArmyBefore = snapshotArmy(*hero);
	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(player);

	GarrisonHeroSwap request(town->id);
	request.player = player;
	request.requestID = 43;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);

	EXPECT_EQ(snapshotArmy(*town), townArmyBefore);
	EXPECT_EQ(snapshotArmy(*hero), heroArmyBefore);
	EXPECT_EQ(town->getVisitingHero(), hero);
	EXPECT_EQ(town->getGarrisonHero(), nullptr);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 1);
}
