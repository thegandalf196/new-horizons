/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleAdventureSpecialtyFixture.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/pathfinder/TurnInfo.h"

namespace
{
using namespace castleSpecialtyTest;

class ForcedMarchSpecialtyTest : public CastleSpecialtyFixture
{
protected:
	int landLimit(const CGHeroInstance * hero, int turn) const
	{
		return hero->getTurnInfo(turn)->getMaxMovePoints(EPathfindingLayer::LAND);
	}
	int seaLimit(const CGHeroInstance * hero, int turn) const
	{
		return hero->getTurnInfo(turn)->getMaxMovePoints(EPathfindingLayer::SAIL);
	}
	int penalties(const CGHeroInstance * hero) const
	{
		int result = 0;
		for(const auto & bonus : hero->getExportedBonusList())
			if(bonus && newHorizonsHeroes::isForcedMarchMoralePenalty(*bonus))
				result += bonus->val;
		return result;
	}
	void advanceTo(CGameHandler & handler, unsigned day)
	{
		while(gameState()->day < day)
			handler.onNewTurn();
	}
};

class HistoricalForcedMarchTest : public ForcedMarchSpecialtyTest
{
protected:
	HistoricalForcedMarchTest()
	{
		historicalSpecialties = true;
	}
};
}

TEST_F(ForcedMarchSpecialtyTest, FirstDayOfEachWeekAddsLevelScaledMovement)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:sylvia"));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	advanceTo(handler, 1);
	EXPECT_EQ(specialist->movementPointsRemaining(), landLimit(specialist, 0)); // The new day refilled the raised limit.
	EXPECT_GT(landLimit(specialist, 0), landLimit(specialist, 1));
	specialist->level = 1;
	const int ordinaryDay = landLimit(specialist, 1); // Day 2.
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*specialist, 0), 20);
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*specialist, 1), 0);
	EXPECT_EQ(landLimit(specialist, 0), ordinaryDay * 120 / 100);
	EXPECT_EQ(landLimit(specialist, 6), ordinaryDay); // Day 7.
	EXPECT_EQ(landLimit(specialist, 7), ordinaryDay * 120 / 100); // Day 8 starts week two.
	EXPECT_EQ(seaLimit(specialist, 0), seaLimit(specialist, 1) * 120 / 100);
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*ordinary, 0), 0);
	specialist->level = 10;
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*specialist, 0), 38);
	specialist->level = 21;
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*specialist, 0), 60);
	specialist->level = 40;
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*specialist, 0), 60);
}

TEST_F(ForcedMarchSpecialtyTest, WeeklyMoralePenaltyLastsUntilTheNextCombatAndNeverStacks)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:sylvia"));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	advanceTo(handler, 1);
	EXPECT_EQ(penalties(specialist), -1);
	EXPECT_EQ(penalties(ordinary), 0);
	const auto moraleWithPenalty = specialist->valOfBonuses(BonusType::MORALE);
	advanceTo(handler, 8); // No combat during week one: one pending penalty remains.
	EXPECT_EQ(penalties(specialist), -1);
	// Combat end removes ONE_BATTLE bonuses from both heroes (BattleResultsApplied).
	specialist->removeBonusesRecursive(Bonus::OneBattle);
	EXPECT_EQ(penalties(specialist), 0);
	EXPECT_EQ(specialist->valOfBonuses(BonusType::MORALE), moraleWithPenalty + 1);
	advanceTo(handler, 9);
	EXPECT_EQ(penalties(specialist), 0); // Only the first day of a week grants it.
	advanceTo(handler, 15);
	EXPECT_EQ(penalties(specialist), -1);
}

TEST_F(ForcedMarchSpecialtyTest, PendingPenaltySurvivesSaveAndLoad)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:sylvia"));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	advanceTo(handler, 1);
	ASSERT_EQ(penalties(specialist), -1);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * reloaded = restored.getHero(specialist->id);
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(penalties(reloaded), -1);
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*reloaded, 0), 20);
	EXPECT_EQ(reloaded->getSpecialtyPresentation().name, "Forced March");
}

TEST_F(ForcedMarchSpecialtyTest, RevisedSylviaLosesTheConvertedLogisticsSpecialty)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:sylvia"));
	EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::LOGISTICS)), 0);
	EXPECT_TRUE(specialist->getExportedBonusList().getFirst(Selector::source(BonusSource::HERO_SPECIAL,
		BonusSourceID(specialist->getHeroTypeID()))) == nullptr);
	EXPECT_EQ(specialist->getSpecialtyPresentation().name, "Forced March");
}

TEST_F(HistoricalForcedMarchTest, HistoricalCaptureKeepsLogisticsAndHasNoWeeklyMarch)
{
	ASSERT_NO_FATAL_FAILURE(startWith("core:sylvia"));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	advanceTo(handler, 1);
	EXPECT_EQ(newHorizonsHeroes::forcedMarchMovementPercent(*specialist, 0), 0);
	EXPECT_EQ(landLimit(specialist, 0), landLimit(specialist, 1));
	EXPECT_EQ(penalties(specialist), 0);
	EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::LOGISTICS)), 20);
	EXPECT_NE(specialist->getSpecialtyPresentation().name, "Forced March");
}

TEST(ForcedMarchSpecialtyRules, ShippedRowIsStrictAndOptional)
{
	JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto rules = newHorizonsHeroes::castleAdventureSpecialties(heroes);
	ASSERT_TRUE(rules && rules->forcedMarchMovementPercent);
	EXPECT_EQ(rules->forcedMarchMovementPercent->at(1), 20);
	EXPECT_EQ(rules->forcedMarchMovementPercent->at(20), 58);
	EXPECT_EQ(rules->forcedMarchMovementPercent->at(21), 60);
	EXPECT_EQ(rules->forcedMarchMoralePenalty, 1);
	auto invalid = heroes;
	invalid["revisedSpecialties"]["forcedMarch"]["maximumMovementPercent"].Integer() = 10;
	EXPECT_THROW(newHorizonsHeroes::validateRevisedSpecialtiesSerialization(invalid), std::runtime_error);
	invalid = heroes;
	invalid["revisedSpecialties"]["forcedMarch"]["extra"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validateRevisedSpecialtiesSerialization(invalid), std::runtime_error);
	heroes["revisedSpecialties"].Struct().erase("forcedMarch");
	const auto without = newHorizonsHeroes::castleAdventureSpecialties(heroes);
	EXPECT_TRUE(!without || !without->forcedMarchMovementPercent);
}
