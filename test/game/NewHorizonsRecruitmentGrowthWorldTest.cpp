/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"
#include "../../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"

namespace
{
constexpr auto option = EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT;
class NewHorizonsRecruitmentGrowthWorldTest : public TinyMapGameTest
{
protected:
	int percent = 50;
	bool useShippedDefaults = false;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires authored New Horizons creature growth";
	}
	Services * gameServices() override { return LIBRARY; }
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		if(!useShippedDefaults)
		{
			loaded->overrideGameSetting(option, JsonNode(percent));
			loaded->overrideGameSetting(EGameSettings::CREATURES_ALLOW_RANDOM_SPECIAL_WEEKS, JsonNode(false));
			loaded->overrideGameSetting(EGameSettings::DWELLINGS_ACCUMULATE_WHEN_OWNED, JsonNode(true));
		}
		for(auto * town : loaded->getObjects<CGTownInstance>())
		{
			town->removeBuilding(BuildingID::DEFAULT);
			town->removeBuilding(BuildingID::FORT);
		}
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.hero({6, 7, 0}, HeroTypeID(1), PlayerColor(1))
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0))
			.dwelling({8, 17, 0}, MapObjectSubID(56), PlayerColor(1));
		startWithMap(std::move(builder));
	}
	CGTownInstance * town() { return findFirst<CGTownInstance>(); }
};
}

TEST_F(NewHorizonsRecruitmentGrowthWorldTest, GeneratedFirstDayAndNextWeekAddHalfWithoutScalingStoredStock)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NE(town(), nullptr);
	const auto sword = CreatureID(CreatureID::decode("core:swordsman"));
	ASSERT_EQ(gameState()->getCreatureBaseGrowth(sword), 7);
	town()->creatures[2] = {17, {sword}}; // Explicit existing stock, not a new growth event.
	const auto angel = CreatureID(CreatureID::decode("core:angel"));
	ASSERT_EQ(gameState()->getCreatureBaseGrowth(angel), 1);
	town()->creatures[6] = {17, {angel}};
	const auto * hero = findFirst<CGHeroInstance>();
	ASSERT_NE(hero, nullptr);
	const auto startingCount = hero->getStackCount(SlotID(0));
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	ASSERT_EQ(gameState()->day, 1);
	EXPECT_EQ(town()->creatures[2].first, 21u); //17stored +ceil7/2.
	EXPECT_EQ(town()->creatures[6].first, 18u); // Singleton growth remains1.
	EXPECT_EQ(town()->creatureGrowth(2), 4);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), startingCount);
	gameState()->day = 7;
	handler.onNewTurn();
	EXPECT_EQ(town()->creatures[2].first, 25u);
	EXPECT_EQ(town()->creatures[6].first, 19u);
}

TEST_F(NewHorizonsRecruitmentGrowthWorldTest, ExternalDwellingAddsFinalHalfAndDoesNotUseNeutralArmyFactorAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGDwelling * dwelling = nullptr;
	for(auto * object : map()->getObjects<CGDwelling>())
		if(object->ID == Obj::CREATURE_GENERATOR1) dwelling = object;
	ASSERT_NE(dwelling, nullptr);
	const auto cavalier = CreatureID(CreatureID::decode("core:cavalier"));
	ASSERT_EQ(gameState()->getCreatureBaseGrowth(cavalier), 3);
	dwelling->creatures = {{17, {cavalier}}};
	EXPECT_EQ(dwelling->normalWeeklyGrowth(0), 2);
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	gameState()->day = 8;
	static_cast<const IObjectInterface *>(dwelling)->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(dwelling->creatures.front().first, 19u);
}

TEST_F(NewHorizonsRecruitmentGrowthWorldTest, StoredStockAndCapturedHalfSurviveTwoWorldLoads)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto sword = CreatureID(CreatureID::decode("core:swordsman"));
	town()->creatures[2] = {37, {sword}};
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(gameState()->saveToMemory());
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(restored->getSettings()), 50);
	EXPECT_EQ(restored->getMap().getObjects<CGTownInstance>().front()->creatures[2].first, 37u);
	auto reread = std::make_shared<CGameState>();
	reread->preInit(LIBRARY);
	reread->loadFromMemory(restored->saveToMemory());
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(reread->getSettings()), 50);
	EXPECT_EQ(reread->getMap().getObjects<CGTownInstance>().front()->creatures[2].first, 37u);
}

TEST_F(NewHorizonsRecruitmentGrowthWorldTest, DisabledGrowthKeepsExistingStockAtFirstDayAndNextWeek)
{
	percent = 0;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto sword = CreatureID(CreatureID::decode("core:swordsman"));
	town()->creatures[2] = {17, {sword}};
	CGDwelling * dwelling = nullptr;
	for(auto * object : map()->getObjects<CGDwelling>())
		if(object->ID == Obj::CREATURE_GENERATOR1) dwelling = object;
	ASSERT_NE(dwelling, nullptr);
	dwelling->creatures = {{17, {CreatureID(CreatureID::decode("core:cavalier"))}}};
	EXPECT_EQ(dwelling->normalWeeklyGrowth(0), 0);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	EXPECT_EQ(town()->creatures[2].first, 17u);
	EXPECT_EQ(dwelling->creatures.front().first, 17u);
	gameState()->day = 7;
	handler.onNewTurn();
	EXPECT_EQ(town()->creatures[2].first, 17u);
	EXPECT_EQ(dwelling->creatures.front().first, 17u);
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(gameState()->getSettings()), 0);
}

TEST_F(NewHorizonsRecruitmentGrowthWorldTest, ReducedCapturedMapWorldAndLobbyRejectOlderWritersBeforeAnyPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(int kind = 0; kind < 3; ++kind)
	{
		CMemorySerializer old;
		old.oser.version = ESerializationVersion::NEW_HORIZONS_GHOST_UPGRADE;
		if(kind == 0)
		{
			EXPECT_THROW(old.oser & *map(), std::runtime_error);
		}
		if(kind == 1)
		{
			EXPECT_THROW(old.oser & *gameState(), std::runtime_error);
		}
		if(kind == 2)
		{
			LobbyStartGame lobby;
			lobby.initializedGameState = gameState();
			EXPECT_THROW(old.oser & lobby, std::runtime_error);
		}
		EXPECT_TRUE(old.extractBuffer().empty());
	}
}

TEST_F(NewHorizonsRecruitmentGrowthWorldTest, NewDwellingConstructionScalesInitialGrantButUpgradePreservesStock)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	ASSERT_TRUE(town()->creatures[0].second.empty());
	ASSERT_EQ(gameState()->getCreatureBaseGrowth(CreatureID(CreatureID::decode("core:pikeman"))), 14);
	ASSERT_TRUE(handler.buildStructure(town()->id, BuildingID::DWELL_LVL_1, true));
	EXPECT_EQ(town()->creatures[0].first, 7u);
	town()->creatures[0].first = 37;
	ASSERT_TRUE(handler.buildStructure(town()->id, BuildingID::DWELL_LVL_1_UP, true));
	EXPECT_EQ(town()->creatures[0].first, 37u);
}

TEST_F(NewHorizonsRecruitmentGrowthWorldTest, ShippedDefaultsWithoutExplicitPercentOverridesProduceAndCaptureHalf)
{
	useShippedDefaults = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsRecruitmentGrowth::percentage(gameState()->getSettings()), 50);
	EXPECT_EQ(newHorizonsNeutralArmies::percentage(gameState()->getSettings()), 50);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	handler.onNewTurn();
	const auto swordsman = CreatureID(CreatureID::decode("core:swordsman"));
	ASSERT_TRUE(town()->creatures[3].second.empty());
	ASSERT_EQ(gameState()->getCreatureBaseGrowth(swordsman), 7);
	ASSERT_TRUE(handler.buildStructure(town()->id, BuildingID::DWELL_LVL_4, true));
	ASSERT_EQ(town()->creatures[3].second, (std::vector<CreatureID>{swordsman}));
	EXPECT_EQ(town()->creatures[3].first, 4u);
}
