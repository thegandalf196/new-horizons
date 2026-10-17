/*
 * VisibleHeroBonusQueryTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OwnedExportMeleeFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/bonuses/BonusList.h"

namespace
{
class ObservedPlayerCallback final : public CPlayerBattleCallback
{
public:
	mutable size_t heroQueryDispatches = 0;
	using CPlayerBattleCallback::CPlayerBattleCallback;
protected:
	TConstBonusListPtr visibleHeroBonuses(BattleSide side, const CSelector & selector, bool unstacked) const override
	{
		++heroQueryDispatches;
		return CBattleInfoCallback::visibleHeroBonuses(side, selector, unstacked);
	}
};
}

class VisibleHeroBonusQueryTest : public OwnedExportMeleeTest
{
protected:
	Bonus heroGrant(BonusType type, const battle::Unit & unit, int value)
	{
		auto record = aura(BonusDuration::ONE_BATTLE, type);
		record.val = value;
		record.subtype = BonusSubtypeID(unit.creatureId());
		record.limiter.reset(); // This helper explicitly grants a hero policy, not a creature-only aura.
		return record;
	}
};

TEST_F(VisibleHeroBonusQueryTest, NativeStackingAndPlayerVisibilityAreAppliedBeforeProviderDispatch)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	auto stronger = heroGrant(BonusType::HERO_GRANTS_ATTACKS, *braced, 4);
	stronger.propagator.reset(); // Direct hero records, not a propagation fixture.
	auto weaker = stronger;
	stronger.stacking = weaker.stacking = "visible-hero-bonus-query.test";
	weaker.val = 2;
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(stronger));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(weaker));
	const CSelector selector = [](const Bonus * record) { return record->stacking == "visible-hero-bonus-query.test"; };
	ObservedPlayerCallback own(battle(), PlayerColor(0)), enemy(battle(), PlayerColor(1)), absent(nullptr, PlayerColor(0));
	const auto stacked = own.battleGetHeroBonuses(BattleSide::ATTACKER, selector);
	const auto raw = own.battleGetHeroBonuses(BattleSide::ATTACKER, selector, true);
	EXPECT_EQ(stacked->toJsonNode(), attackerSideHero->getAllBonuses(selector)->toJsonNode());
	EXPECT_EQ(raw->toJsonNode(), attackerSideHero->getUnstackedBonuses(selector)->toJsonNode());
	EXPECT_EQ(stacked->totalValue(), 4);
	EXPECT_EQ(raw->totalValue(), 6);
	EXPECT_EQ(own.heroQueryDispatches, 2);
	EXPECT_TRUE(enemy.battleGetHeroBonuses(BattleSide::ATTACKER, selector)->empty());
	EXPECT_TRUE(own.battleGetHeroBonuses(BattleSide::INVALID, selector)->empty());
	EXPECT_TRUE(absent.battleGetHeroBonuses(BattleSide::ATTACKER, selector)->empty());
	EXPECT_EQ(enemy.heroQueryDispatches, 0);
	EXPECT_EQ(own.heroQueryDispatches, 2);
	EXPECT_EQ(absent.heroQueryDispatches, 0);
}

TEST_F(VisibleHeroBonusQueryTest, OwnedCountUsesOriginatingAddRemovalAndSiblingHistoryWithoutNativeFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	const auto nativeCount = AttackPossibility::getAttackCount(*incoming, false, *battle());
	ASSERT_NO_FATAL_FAILURE(captureOwned());
	ASSERT_NO_FATAL_FAILURE(startOwned());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
	const auto grant = heroGrant(BonusType::HERO_GRANTS_ATTACKS, *incoming, 2);
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	known->addUnitBonus(incoming->unitId(), {grant});
	const auto * actor = known->battleGetUnitByID(incoming->unitId());
	EXPECT_EQ(AttackPossibility::getAttackCount(*actor, false, *known), nativeCount + 2);
	CPlayerBattleCallback visible(known.get(), PlayerColor(1)), hidden(known.get(), PlayerColor(0));
	EXPECT_EQ(AttackPossibility::getAttackCount(*actor, false, visible), nativeCount + 2);
	EXPECT_EQ(AttackPossibility::getAttackCount(*actor, false, hidden), actor->getTotalAttacks(false));
	EXPECT_TRUE(hidden.battleGetHeroBonuses(BattleSide::DEFENDER, Selector::all)->empty());
	EXPECT_EQ(AttackPossibility::getAttackCount(*sibling->battleGetUnitByID(incoming->unitId()), false, *sibling), nativeCount);
	EXPECT_EQ(AttackPossibility::getAttackCount(*incoming, false, *battle()), nativeCount);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), known);
	EXPECT_EQ(AttackPossibility::getAttackCount(*child->battleGetUnitByID(incoming->unitId()), false, *child), nativeCount + 2);
	known->removeUnitBonus(incoming->unitId(), {grant});
	EXPECT_EQ(AttackPossibility::getAttackCount(*actor, false, *known), nativeCount);
	EXPECT_EQ(AttackPossibility::getAttackCount(*child->battleGetUnitByID(incoming->unitId()), false, *child), nativeCount + 2);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(VisibleHeroBonusQueryTest, OwnedCountMatchesTheActualAuthoritativeFixedIncomingLoop)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false)); // Isolate count from retaliation death truncation.
	ASSERT_EQ(defenderSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(incoming->creatureId())), 0);
	ASSERT_NO_FATAL_FAILURE(captureOwned());
	const auto grant = heroGrant(BonusType::HERO_GRANTS_ATTACKS, *incoming, 2);
	addOrigin(*incoming, grant);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	const int predicted = AttackPossibility::getAttackCount(*known->battleGetUnitByID(incoming->unitId()), false, *known);
	ASSERT_EQ(predicted, incoming->getTotalAttacks(false) + 2);
	known.reset();
	seed.reset(); // No query provider remains when native graph/actions mutate.
	incoming->addNewBonus(std::make_shared<Bonus>(grant));
	ASSERT_EQ(AttackPossibility::getAttackCount(*incoming, false, *battle()), predicted);
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
	EXPECT_EQ(std::ranges::count_if(server.attacks, [this](const BattleAttack & hit)
	{
		return hit.stackAttacking == incoming->unitId() && !hit.counter();
	}), predicted);
}

TEST_F(VisibleHeroBonusQueryTest, HiddenOpponentAndRangedFollowUpDoNotDispatchHeroBonusQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, true));
	releaseKnown();
	auto * ballista = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(70), 1);
	ASSERT_TRUE(ballista->isBallista());
	ballista->rangedFollowUpDamagePercent = 50;
	ASSERT_GT(ballista->shots.available(), 0);
	ObservedPlayerCallback own(battle(), PlayerColor(0)), enemy(battle(), PlayerColor(1));
	EXPECT_EQ(AttackPossibility::getAttackCount(*ballista, true, own), 1);
	EXPECT_EQ(own.heroQueryDispatches, 0);
	EXPECT_EQ(AttackPossibility::getAttackCount(*braced, false, enemy), braced->getTotalAttacks(false));
	EXPECT_EQ(enemy.heroQueryDispatches, 0);
}

TEST_F(VisibleHeroBonusQueryTest, ProjectedBonusDamageFlagIsVisibleAtTheRealBraceStageButDoesNotRemoveTheFullWitness)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	ASSERT_NO_FATAL_FAILURE(captureOwned());
	const auto chance = heroGrant(BonusType::BONUS_DAMAGE_CHANCE, *braced, 100);
	addOrigin(*braced, chance);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	known->moveUnit(incoming->unitId(), BattleHex(92));
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto forecast = heroOrderValue::forecastBraceAtStage(*known, {incoming->unitId(), braced->unitId(), 3});
	ASSERT_TRUE(forecast.event);
	EXPECT_EQ(forecast.coverage, heroOrderValue::BraceStageCoverage::NEEDS_PRE_HIT_PROJECTION);
	EXPECT_FALSE(forecast.primary);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::BONUS_DAMAGE_CHANCE, BonusSubtypeID(braced->creatureId())), 0);
	EXPECT_FALSE(heroOrderValue::prepareBraceStage(environment.get(), known, action()));
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
}

TEST_F(VisibleHeroBonusQueryTest, UnitDoubleDamageFlagKeepsTheHeroBonusQueryLazy)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence());
	releaseKnown();
	braced->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::DOUBLE_DAMAGE_CHANCE,
		BonusSource::OTHER, 100, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(refreshKnown());
	known->moveUnit(incoming->unitId(), BattleHex(92));
	ObservedPlayerCallback callback(known.get(), PlayerColor(0));
	const auto forecast = heroOrderValue::forecastBraceAtStage(callback, {incoming->unitId(), braced->unitId(), 3});
	ASSERT_TRUE(forecast.event);
	EXPECT_EQ(forecast.coverage, heroOrderValue::BraceStageCoverage::NEEDS_PRE_HIT_PROJECTION);
	EXPECT_EQ(callback.heroQueryDispatches, 0);
}
