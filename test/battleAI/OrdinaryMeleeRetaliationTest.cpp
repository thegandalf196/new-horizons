/*
 * OrdinaryMeleeRetaliationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/mock_battle_Unit.h"
#include "../../lib/battle/OrdinaryMeleeRetaliation.h"

TEST(OrdinaryMeleeRetaliationTest, NativeTruthTableAndLazyUnitQueryOrder)
{
	for(unsigned mask = 0; mask < 1024; ++mask)
	{
		SCOPED_TRACE(mask);
		const bool alive = mask & 1, invincible = mask & 2, able = mask & 4;
		const battle::OrdinaryMeleeRetaliationContext context{bool(mask & 8), bool(mask & 16),
			bool(mask & 32), mask & 64 ? 1 : 0, bool(mask & 128), bool(mask & 256), bool(mask & 512)};
		testing::StrictMock<UnitMock> actor, victim;
		testing::InSequence ordered;
		EXPECT_CALL(actor, alive()).WillOnce(testing::Return(alive));
		const bool reachesInvincibility = alive && !context.counterBlocked;
		if(reachesInvincibility)
			EXPECT_CALL(actor, isInvincible()).WillOnce(testing::Return(invincible));
		const bool reachesRetaliation = reachesInvincibility && !invincible
			&& !context.longWeaponAttack && !context.longReachAttack
			&& context.attackIndex == 0 && !context.firstStrike && !context.shroudBlocked && !context.frozenBeforeHit;
		if(reachesRetaliation)
			EXPECT_CALL(victim, ableToRetaliate()).WillOnce(testing::Return(able));
		EXPECT_EQ(battle::canRetaliateAfterOrdinaryMelee(actor, victim, context), reachesRetaliation && able);
	}
}
