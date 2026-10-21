/*
 * NewHorizonsBattlecraftPreemptiveStrikeAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattlecraftPreemptiveAIFixture.h"

namespace
{
class NewHorizonsBattlecraftPreemptiveStrikeAITest : public battlecraftTest::PreemptiveFixture
{
};
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeAITest, ForecastConsumesTheDefendMarkerOnlyInTheDetachedExchange)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(defenderSideHero));
	startBattle();
	beginCombat();
	auto * firstAttacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(5, 5), 1000);
	auto * secondAttacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(7, 5), 1000);
	auto * defender = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(6, 5), 1000);
	ASSERT_NE(firstAttacker, nullptr);
	ASSERT_NE(secondAttacker, nullptr);
	ASSERT_NE(defender, nullptr);
	removeOtherStacks({firstAttacker, secondAttacker, defender});
	forceMaximumDamage(defender);
	blockRetaliation(defender);
	blockRetaliation(firstAttacker);
	blockRetaliation(secondAttacker);
	ASSERT_TRUE(defend(defender));
	ASSERT_EQ(defender->battlecraftPreemptiveStrikeRound, -1);
	battle()->activeStack = firstAttacker->unitId();
	initializeAI(PlayerColor(1));

	const auto firstHealth = firstAttacker->getAvailableHealth();
	const auto defenderHealth = defender->getAvailableHealth();
	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedFirst = model->battleGetUnitByID(firstAttacker->unitId());
	const auto * projectedDefender = model->battleGetUnitByID(defender->unitId());
	ASSERT_NE(projectedFirst, nullptr);
	ASSERT_NE(projectedDefender, nullptr);
	const BattleAttackInfo incoming(projectedFirst, projectedDefender, 0, false);
	BattleAttackInfo expectedReaction(projectedDefender, projectedFirst, 0, false);
	expectedReaction.retaliation = true;
	expectedReaction.preemptiveDamagePercent = 50;
	const auto expectedDamage = std::min(firstHealth, model->battleExpectedLuckDamage(expectedReaction));
	ASSERT_GT(expectedDamage, 0);

	const auto possibility = AttackPossibility::evaluate(incoming,
		projectedFirst->getPosition(), damageCache, model);
	ASSERT_NE(possibility.effectPreview, nullptr);
	ASSERT_NE(possibility.attackerState, nullptr);
	EXPECT_EQ(possibility.attackerState->getAvailableHealth(), firstHealth - expectedDamage)
		<< "The detached pre-hit is forecast before the incoming primary melee damage";
	EXPECT_EQ(possibility.effectPreview->getForUpdate(defender->unitId())->battlecraftPreemptiveStrikeRound,
		battle()->getRound());
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, -1)
		<< "Evaluating an attack must not mutate the live battle unit";
	EXPECT_EQ(firstAttacker->getAvailableHealth(), firstHealth);
	EXPECT_EQ(defender->getAvailableHealth(), defenderHealth);

	BattleExchangeVariant exchange;
	exchange.trackAttack(possibility, model, damageCache);
	EXPECT_EQ(model->getForUpdate(defender->unitId())->battlecraftPreemptiveStrikeRound,
		battle()->getRound())
		<< "Accepted exchange replay carries the consumed round marker to later projections";
	EXPECT_EQ(defender->battlecraftPreemptiveStrikeRound, -1);

	DamageCache secondDamageCache;
	secondDamageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedSecond = model->battleGetUnitByID(secondAttacker->unitId());
	const auto * projectedTarget = model->battleGetUnitByID(defender->unitId());
	const auto second = AttackPossibility::evaluate(BattleAttackInfo(projectedSecond, projectedTarget, 0, false),
		projectedSecond->getPosition(), secondDamageCache, model);
	EXPECT_NEAR(second.attackerDamageReduce, 0.0f, 0.001f)
		<< "A second incoming melee in the same round cannot receive the pre-hit again";
}

TEST_F(NewHorizonsBattlecraftPreemptiveStrikeAITest, ActivePerkCanMakeAnImmobileStackChooseDefendWithoutMutatingIt)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectPreemptiveStrike(attackerSideHero));
	startBattle();
	beginCombat();
	auto * activeStack = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(5, 5), 10);
	auto * threat = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(8, 5), 5);
	ASSERT_NE(activeStack, nullptr);
	ASSERT_NE(threat, nullptr);
	removeOtherStacks({activeStack, threat});
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -activeStack->getMovementRange(0), BonusSourceID());
	activeStack->addNewBonus(std::make_shared<Bonus>(immobilized));
	ASSERT_EQ(activeStack->getMovementRange(0), 0);
	battle()->activeStack = activeStack->unitId();
	initializeAI(PlayerColor(0));

	const auto action = choose(activeStack);
	EXPECT_EQ(action.actionType, EActionType::DEFEND)
		<< "When its only credible use is defense, AI should account for the projected pre-hit threat";
	EXPECT_FALSE(activeStack->defended())
		<< "AI valuation does not set Defend on the live unit";
	EXPECT_EQ(activeStack->battlecraftPreemptiveStrikeRound, -1);
	EXPECT_FALSE(threat->defended());
}
