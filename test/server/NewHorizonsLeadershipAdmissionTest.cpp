/*
 * NewHorizonsLeadershipAdmissionTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>
#include <string>
#include <tuple>

#include "../../lib/GameConstants.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
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
	}

	std::vector<PackageApplied> responses;
	std::vector<std::string> systemMessageTexts;
	int systemMessages = 0;
	int rebalancePacks = 0;

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
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
            JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
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
