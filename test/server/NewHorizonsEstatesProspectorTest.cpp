/*
 * NewHorizonsEstatesProspectorTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"
#include "battles/FullGameSnapshotTypes.h"

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES = "new-horizons:estates";
constexpr auto PROSPECTOR = "new-horizons:estates.prospector";

class RecordingServer : public GameHandlerTestServer
{
public:
	explicit RecordingServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PLAYER)
	{}
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * property = dynamic_cast<const SetObjectProperty *>(&pack);
			property && property->what == ObjProperty::NEW_HORIZONS_PROSPECTOR_LAST_WEEK)
			lastReceipt = *property;
		GameHandlerTestServer::applyPack(pack);
	}
	std::optional<SetObjectProperty> lastReceipt;
};

class NewHorizonsEstatesProspectorTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode days;
		days.Integer() = 7;
		loaded->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, days);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.hero({4, 5, 0}, HeroTypeID(0), PLAYER)
			.hero({4, 24, 0}, HeroTypeID(1), PLAYER);
		for(int resource = 0; resource < 7; ++resource)
			builder.mine({10 + 3 * resource, 10, 0}, MapObjectSubID(resource), PLAYER);
		builder.mine({10, 20, 0}, MapObjectSubID(0), PlayerColor::NEUTRAL);
		builder.mine({15, 20, 0}, MapObjectSubID(0), PlayerColor::NEUTRAL);
		startWithMap(std::move(builder));
		first = findHeroAt({4, 5, 0});
		second = findHeroAt({4, 24, 0});
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		for(int resource = 0; resource < 7; ++resource)
		{
			mines[resource] = dynamic_cast<CGMine *>(findObjectAt({10 + 3 * resource, 10, 0}));
			ASSERT_NE(mines[resource], nullptr);
		}
		neutral = dynamic_cast<CGMine *>(findObjectAt({10, 20, 0}));
		ASSERT_NE(neutral, nullptr);
	}
	bool acceptPerk(CGameHandler & handler, CGHeroInstance * hero, const std::string & perkId = PROSPECTOR)
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(ESTATES));
		handler.changeSecSkill(hero, skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto rank = [hero](const std::string & key) { return hero->getPerkSkillRank(key); };
		for(uint64_t seed = 0; seed < 256; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed);
			const auto selected = std::ranges::find_if(offer, [&](const auto & item)
			{
				return item.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;
			if(selected->requiredRank != MasteryLevel::BASIC)
				return false;
			handler.levelUpHero(hero, offer, static_cast<size_t>(std::distance(offer.begin(), selected)), seed, false);
			return hero->hasActivePerk(ESTATES, perkId);
		}
		return false;
	}
	void visit(CGameHandler & handler, CGHeroInstance * hero, CGMine * mine)
	{
		handler.objectVisited(mine, hero);
		while(const auto query = handler.queries->topQuery(PLAYER))
			handler.queries->popQuery(query);
	}
	int week() const
	{
		const auto calendar = gameState()->getCalendar();
		return newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	}
	ResourceSet resources() const { return gameState()->getPlayerState(PLAYER)->resources; }
	std::string hover(CGHeroInstance * hero, CGMine * mine) const
	{
		return static_cast<const CGObjectInstance *>(mine)->getHoverText(hero).toString(LIBRARY->generaltexth.get());
	}
	std::array<CGMine *, 7> mines{};
	CGMine * neutral = nullptr;
	CGHeroInstance * first = nullptr;
	CGHeroInstance * second = nullptr;
};

TEST_F(NewHorizonsEstatesProspectorTest, AcceptedBasicPerkPaysTwoCommonResources)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	ASSERT_TRUE(acceptPerk(handler, second));
	const auto before = resources();
	visit(handler, first, mines[GameResID::WOOD]);
	visit(handler, second, mines[GameResID::ORE]);
	ResourceSet expected;
	expected[GameResID::WOOD] = 2;
	expected[GameResID::ORE] = 2;
	EXPECT_EQ(resources() - before, expected);
	EXPECT_TRUE(first->hasUsedNewHorizonsProspector(week()));
	EXPECT_EQ(makeCallback(PLAYER)->getResourceAmount(), resources());
}

TEST_F(NewHorizonsEstatesProspectorTest, EachRareResourcePaysOneOnItsFirstWeeklyVisit)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	for(const auto resource : {GameResID(GameResID::MERCURY), GameResID(GameResID::SULFUR),
		GameResID(GameResID::CRYSTAL), GameResID(GameResID::GEMS)})
	{
		const auto before = resources();
		visit(handler, first, mines[resource.getNum()]);
		ResourceSet expected;
		expected[resource] = 1;
		EXPECT_EQ(resources() - before, expected);
		const auto currentWeek = week();
		while(week() == currentWeek)
			handler.onNewTurn();
	}
}

TEST_F(NewHorizonsEstatesProspectorTest, GoldIsExcludedWithoutConsumingTheWeeklyUse)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	const auto before = resources();
	visit(handler, first, mines[GameResID::GOLD]);
	EXPECT_EQ(resources(), before);
	EXPECT_EQ(first->getNewHorizonsProspectorLastWeek(), -1);
	EXPECT_FALSE(server.lastReceipt);
	visit(handler, first, mines[GameResID::WOOD]);
	EXPECT_EQ(resources()[GameResID::WOOD] - before[GameResID::WOOD], 2);
}

TEST_F(NewHorizonsEstatesProspectorTest, RepeatedVisitsDoNotPayButAnotherHeroHasItsOwnUse)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	ASSERT_TRUE(acceptPerk(handler, second));
	visit(handler, first, mines[0]);
	const auto after = resources();
	visit(handler, first, mines[0]);
	visit(handler, first, mines[2]);
	EXPECT_EQ(resources(), after);
	visit(handler, second, mines[0]);
	EXPECT_EQ(resources()[GameResID::WOOD] - after[GameResID::WOOD], 2);
}

TEST_F(NewHorizonsEstatesProspectorTest, ActualNewWeekRestoresEligibilityWithoutResettingHistory)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	visit(handler, first, mines[0]);
	const auto usedWeek = week();
	while(week() == usedWeek)
	{
		handler.onNewTurn();
		EXPECT_EQ(first->getNewHorizonsProspectorLastWeek(), usedWeek);
	}
	const auto before = resources();
	visit(handler, first, mines[0]);
	EXPECT_EQ(resources()[GameResID::WOOD] - before[GameResID::WOOD], 2);
	EXPECT_EQ(first->getNewHorizonsProspectorLastWeek(), week());
}

TEST_F(NewHorizonsEstatesProspectorTest, NoPerkAndCaptureDoNotPayAndTooltipUsesSavedHeroReceipt)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto before = resources();
	visit(handler, second, mines[0]);
	EXPECT_EQ(resources(), before);
	ASSERT_TRUE(acceptPerk(handler, first));
	ASSERT_TRUE(acceptPerk(handler, second, "new-horizons:estates.landSurveyor"));
	const auto readyText = hover(first, mines[0]);
	EXPECT_NE(readyText.find("+2"), std::string::npos);
	visit(handler, first, neutral);
	EXPECT_EQ(resources(), before);
	EXPECT_FALSE(server.lastReceipt);
	EXPECT_EQ(first->getNewHorizonsProspectorLastWeek(), -1);
	EXPECT_FALSE(first->hasUsedNewHorizonsLandSurveyor(week()));
	EXPECT_EQ(neutral->getOwner(), PLAYER);
	auto * landMine = dynamic_cast<CGMine *>(findObjectAt({15, 20, 0}));
	ASSERT_NE(landMine, nullptr);
	visit(handler, second, landMine);
	EXPECT_EQ(resources() - before, landMine->dailyIncome() * 3);
	EXPECT_TRUE(second->hasUsedNewHorizonsLandSurveyor(week()));
	EXPECT_EQ(second->getNewHorizonsProspectorLastWeek(), -1);
	const auto beforeOwnedVisit = resources();
	visit(handler, first, mines[0]);
	ResourceSet ownedReward;
	ownedReward[GameResID::WOOD] = 2;
	EXPECT_EQ(resources() - beforeOwnedVisit, ownedReward);
	EXPECT_NE(hover(first, mines[0]).find("already received"), std::string::npos);
	EXPECT_NE(hover(first, mines[6]).find("neither common nor rare"), std::string::npos);
	EXPECT_NE(hover(first, mines[0]), readyText);
	EXPECT_EQ(second->getNewHorizonsProspectorLastWeek(), -1);
}

TEST_F(NewHorizonsEstatesProspectorTest, SaveLoadAndActualReplicatedPropertyPreserveReceipt)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	visit(handler, first, mines[0]);
	ASSERT_TRUE(server.lastReceipt);
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * hero = restored.getHero(first->id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getNewHorizonsProspectorLastWeek(), week());
	EXPECT_EQ(restored.getPlayerState(PLAYER)->resources, resources());
	SetObjectProperty clear = *server.lastReceipt;
	clear.identifier = NumericID(-1);
	restored.apply(clear);
	EXPECT_EQ(hero->getNewHorizonsProspectorLastWeek(), -1);
	const auto packet = CMemorySerializer::deepCopy<CPackForClient>(*server.lastReceipt);
	restored.apply(*packet);
	EXPECT_EQ(hero->getNewHorizonsProspectorLastWeek(), week());
}

TEST_F(NewHorizonsEstatesProspectorTest, OldWriterAndMalformedReceiptRejectBeforePropertyPrefix)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	visit(handler, first, mines[0]);
	CMemorySerializer oldHero;
	oldHero.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
	EXPECT_THROW(oldHero.oser & *first, std::runtime_error);
	EXPECT_TRUE(oldHero.extractBuffer().empty());

	SetObjectProperty property;
	property.id = ObjectInstanceID(0);
	property.what = ObjProperty::NEW_HORIZONS_PROSPECTOR_LAST_WEEK;
	property.identifier = NumericID(0);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
	EXPECT_THROW(old.oser & property, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	property.identifier = NumericID(-2);
	CMemorySerializer malformed;
	EXPECT_THROW(malformed.oser & property, std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
	for(const ObjPropertyID wrong : {ObjPropertyID(PlayerColor(0)), ObjPropertyID(ObjectInstanceID(0))})
	{
		property.identifier = wrong;
		CMemorySerializer wrongType;
		EXPECT_THROW(wrongType.oser & property, std::runtime_error);
		EXPECT_TRUE(wrongType.extractBuffer().empty());
		const auto receiptBefore = first->getNewHorizonsProspectorLastWeek();
		EXPECT_THROW(first->setProperty(ObjProperty::NEW_HORIZONS_PROSPECTOR_LAST_WEEK, wrong), std::runtime_error);
		EXPECT_EQ(first->getNewHorizonsProspectorLastWeek(), receiptBefore);

		// Build hostile wire bytes below the guarded object serializer to verify
		// a reader also rejects a well-formed but wrong identifier alternative.
		CMemorySerializer incoming;
		incoming.oser & property.id;
		incoming.oser & property.what;
		incoming.oser & property.identifier;
		SetObjectProperty decoded;
		EXPECT_THROW(incoming.iser & decoded, std::runtime_error);
	}
	property.what = ObjProperty::NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK;
	property.identifier = NumericID(0);
	CMemorySerializer ordinary;
	ordinary.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
	EXPECT_NO_THROW(ordinary.oser & property);
}

TEST_F(NewHorizonsEstatesProspectorTest, WorldMapAndLobbyPreflightOnMapAndActualOffMapPoolReceipts)
{
	startGame();
	RecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(acceptPerk(handler, first));
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();

	// An unused receipt is representable in the immediately previous format.
	CMemorySerializer unusedWorld;
	unusedWorld.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
	EXPECT_NO_THROW(unusedWorld.oser & *gameState());
	EXPECT_FALSE(unusedWorld.extractBuffer().empty());
	CMemorySerializer unusedLobby;
	unusedLobby.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
	EXPECT_NO_THROW(unusedLobby.oser & lobby);
	EXPECT_FALSE(unusedLobby.extractBuffer().empty());

	visit(handler, first, mines[0]);
	const auto rejectBeforePrefixes = [&]()
	{
		CMemorySerializer worldWriter;
		worldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
		EXPECT_THROW(worldWriter.oser & *gameState(), std::runtime_error);
		EXPECT_TRUE(worldWriter.extractBuffer().empty());
		CMemorySerializer mapWriter;
		mapWriter.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
		EXPECT_THROW(mapWriter.oser & *map(), std::runtime_error);
		EXPECT_TRUE(mapWriter.extractBuffer().empty());
		CMemorySerializer lobbyWriter;
		lobbyWriter.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
		EXPECT_THROW(lobbyWriter.oser & lobby, std::runtime_error);
		EXPECT_TRUE(lobbyWriter.extractBuffer().empty());
	};
	rejectBeforePrefixes();

	// Use the actual CMap hero pool, not TavernHeroesPool's ID-only index.
	const auto hero = std::dynamic_pointer_cast<CGHeroInstance>(map()->eraseObject(first->id));
	ASSERT_NE(hero, nullptr);
	map()->addToHeroPool(hero);
	ASSERT_EQ(map()->tryGetFromHeroPool(hero->getHeroTypeID()), hero.get());
	ASSERT_EQ(map()->objects[first->id.getNum()], nullptr);
	rejectBeforePrefixes();
	EXPECT_NO_THROW(gameState()->validateNewHorizonsProspectorSerialization(true));
	EXPECT_NO_THROW(lobby.validateNewHorizonsProspectorSerialization(true));

	// Empty lobby starts have no world receipt and keep their old wire shape.
	LobbyStartGame emptyLobby;
	CMemorySerializer empty;
	empty.oser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
	EXPECT_NO_THROW(empty.oser & emptyLobby);
}
} // namespace
