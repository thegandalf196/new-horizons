/*
 * FrozenAttackCommitCharacterizationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "FrozenAttackStageFixture.h"

TEST_F(FrozenAttackStageFixture, IceShatteringAnAlreadyFrozenTargetStillSpendsOneProcRoll)
{
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = ice->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	freezeTarget();
	const auto receipt = target->frozenLastAppliedRound();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		ASSERT_FALSE(gameHandler->randomizer->rollCombatAbility(battle()->getSideArmy(side)->id, 0));
	int procSeed = 0;
	for(; procSeed < 4096; ++procSeed)
	{
		CRandomGenerator candidate(procSeed);
		if(candidate.nextInt(0, 99) < 20)
			break;
	}
	ASSERT_LT(procSeed, 4096);
	CRandomGenerator expected(procSeed);
	expected.nextInt(0, 99);
	gameHandler->randomizer->setSeed(procSeed);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(ice, target->getPosition(), ice->getPosition())));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), receipt);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expected.nextInt());
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_TRUE(server.attacks.front().bsa.front().shattered());
	EXPECT_EQ(server.attacks.front().stackAttacking, ice->unitId());
}
