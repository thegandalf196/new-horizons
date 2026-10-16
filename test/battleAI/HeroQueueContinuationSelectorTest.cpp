/*
 * HeroQueueContinuationSelectorTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/NewHorizonsPlague.h"

namespace heroQueueTest
{
TEST_F(HeroQueueSelectorTest, EarnedPursuitMovementReceiptSurvivesRealSelectorEvaluation)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:offense",
		"new-horizons:offense.pursuit", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(arena());
	const auto * reserve = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 1000);
	ASSERT_NE(reserve, nullptr);
	ASSERT_NO_FATAL_FAILURE(activate(actor));
	const auto forecast = battle()->calculateDmgRange(BattleAttackInfo(actor, enemy, 0, false)).damage;
	ASSERT_GT(forecast.min, enemy->getAvailableHealth());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, enemy->getPosition(), actor->getPosition())));
	ASSERT_FALSE(enemy->alive());
	ASSERT_GT(actor->pursuitMovementRemaining, 0);
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_FALSE(server.stackActivations.empty());
	ASSERT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::PURSUIT_CONTINUATION);
	active = actor;
	const auto allowance = actor->pursuitMovementRemaining;
	RecordProperty("earnedPursuitMovement", std::to_string(allowance));
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_EQ(actor->pursuitMovementRemaining, allowance);
	// This witnesses existing continuation isolation, not the queue's positive
	// completion-exclusion guard: TURN_QUEUE nextTurn resets the old budget.
}

TEST_F(HeroQueueSelectorTest, EarnedMasterGunnerFollowupAndPlagueStayLiveDuringSelectorForecast)
{
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, "new-horizons:warMachines",
		"new-horizons:warMachines.masterGunner", MasteryLevel::BASIC));
	const SpellID plague(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	ASSERT_NO_FATAL_FAILURE(book(defenderSideHero, plague, "new-horizons:shadowMagic"));
	ASSERT_NO_FATAL_FAILURE(arena());
	auto * ballista = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(2, 1), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 1), 10000);
	ASSERT_NE(ballista, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(cast(defenderSideHero, plague, ballista));
	ASSERT_TRUE(newHorizonsPlague::hasPlague(ballista));
	ASSERT_NO_FATAL_FAILURE(activate(ballista));
	ASSERT_TRUE(battle()->battleCanShoot(ballista, target->getPosition()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(ballista, target)));
	ASSERT_EQ(battle()->battleActiveUnit(), ballista);
	ASSERT_TRUE(battle()->battleHasPendingRangedFollowUp(ballista));
	ASSERT_EQ(ballista->rangedFollowUpDamagePercent, 60);
	ASSERT_FALSE(server.stackActivations.empty());
	ASSERT_EQ(server.stackActivations.back().reason, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION);
	active = ballista;
	ASSERT_NO_FATAL_FAILURE(characterize(true));
	EXPECT_TRUE(battle()->battleHasPendingRangedFollowUp(ballista));
	EXPECT_TRUE(newHorizonsPlague::hasPlague(ballista));
	EXPECT_EQ(ballista->rangedFollowUpDamagePercent, 60);
}
}
