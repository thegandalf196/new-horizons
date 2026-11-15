/*
 * NewHorizonsMovementLevelGrowthTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameConstants.h"
#include "../../lib/StartInfo.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/pathfinder/TurnInfo.h"

class NewHorizonsMovementLevelGrowthGame : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		settings.bonus = PlayerStartingBonus::GOLD;
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
		{
			rules.Struct().erase("dailyMovement");
			// Model a captured profile, not a patch merged with current defaults.
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		ASSERT_NE(hero, nullptr);
		ASSERT_TRUE(hero->usesNewHorizonsMovement());
	}

	void addBonus(const bool water, const int value, const BonusValueType valueType)
	{
		hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
			BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(water
				? BonusCustomSubtype::heroMovementSea : BonusCustomSubtype::heroMovementLand), valueType));
	}

	CGHeroInstance * hero = nullptr;
	bool historical = false;
};

TEST_F(NewHorizonsMovementLevelGrowthGame, SharedLiveForecastAndTooltipUseGrowthWithoutRefilling)
{
	startGame();
	hero->setMovementPoints(73);
	TurnInfoCache cache(hero);
	for(const auto [level, expected] : {std::pair{1u, 200}, {10u, 209}, {20u, 219}, {30u, 229}})
	{
		hero->level = level;
		hero->nodeHasChanged();
		const TurnInfo info(&cache, hero, 0);
		EXPECT_EQ(info.getMaxMovePoints(EPathfindingLayer::LAND), expected);
		EXPECT_EQ(info.getMaxMovePoints(EPathfindingLayer::SAIL), expected);
		EXPECT_EQ(hero->movementPointsLimit(), expected);
		for(const bool water : {false, true})
		{
			const auto detail = newHorizonsMovement::currentDailyMovementBreakdown(hero, water);
			ASSERT_TRUE(detail);
			EXPECT_EQ(detail->limit, expected);
			EXPECT_EQ(detail->levelGrowth, level - 1);
		}
		EXPECT_EQ(hero->movementPointsRemaining(), 73);
	}
}

TEST_F(NewHorizonsMovementLevelGrowthGame, GrowthFollowsLogisticsStageAndPrecedesPercentageToAll)
{
	startGame();
	hero->level = 20;
	for(const bool water : {false, true})
		addBonus(water, 30, BonusValueType::PERCENT_TO_BASE);
	EXPECT_EQ(hero->movementPointsLimit(), 279); // 200 * 1.30 + 19
	for(const bool water : {false, true})
		addBonus(water, 10, BonusValueType::PERCENT_TO_ALL);
	TurnInfoCache cache(hero);
	const TurnInfo info(&cache, hero, 0);
	EXPECT_EQ(info.getMaxMovePoints(EPathfindingLayer::LAND), 306);
	EXPECT_EQ(info.getMaxMovePoints(EPathfindingLayer::SAIL), 306);
	EXPECT_EQ(newHorizonsMovement::currentDailyMovementBreakdown(hero, false)->limit, 306);
}

TEST_F(NewHorizonsMovementLevelGrowthGame, SaveLoadKeepsCapturedProfileAndSpentMovement)
{
	startGame();
	hero->setMovementPoints(73);
	const auto captured = hero->getPrimaryGrowthRules();
	ASSERT_EQ(captured["dailyMovement"]["pointsPerLevel"].Integer(), 1);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getPrimaryGrowthRules(), captured);
	EXPECT_EQ(restoredHero->movementPointsRemaining(), 73);
	EXPECT_EQ(restoredHero->movementPointsLimit(), hero->movementPointsLimit());
}

TEST_F(NewHorizonsMovementLevelGrowthGame, HistoricalCapturedAbsenceRemainsUnchangedAcrossLoad)
{
	historical = true;
	startGame();
	ASSERT_FALSE(hero->getPrimaryGrowthRules().Struct().contains("dailyMovement"));
	const auto captured = hero->getPrimaryGrowthRules();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getPrimaryGrowthRules(), captured);
	EXPECT_FALSE(restoredHero->getPrimaryGrowthRules().Struct().contains("dailyMovement"));
	restoredHero->level = 30;
	restoredHero->nodeHasChanged();
	EXPECT_EQ(restoredHero->movementPointsLimit(), 200);
	EXPECT_EQ(newHorizonsMovement::currentDailyMovementBreakdown(restoredHero, true)->limit, 200);
}
