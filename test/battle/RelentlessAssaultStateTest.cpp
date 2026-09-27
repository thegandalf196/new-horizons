/*
 * RelentlessAssaultStateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/battle/RelentlessAssaultState.h"

TEST(RelentlessAssaultState, ConsecutiveActivationsScaleAndCapAtThirtyPercent)
{
	RelentlessAssaultState state;
	state.beginActivation();
	EXPECT_EQ(state.damagePercentForTarget(11), 0);
	state.recordAttack(11);
	EXPECT_EQ(state.tier, 0);

	for(const int expectedBonus : {10, 20, 30, 30})
	{
		state.beginActivation();
		EXPECT_EQ(state.damagePercentForTarget(11), expectedBonus);
		state.recordAttack(11);
		EXPECT_EQ(state.activationDamagePercent, expectedBonus);
	}
	EXPECT_EQ(state.tier, 3);
}

TEST(RelentlessAssaultState, RepeatedHitsInOneActivationKeepTierAndNewTargetResetsIt)
{
	RelentlessAssaultState state;
	state.beginActivation();
	state.recordAttack(11);
	state.beginActivation();
	state.recordAttack(11);
	ASSERT_EQ(state.tier, 1);
	EXPECT_EQ(state.damagePercentForTarget(11), 10);
	state.recordAttack(11);
	EXPECT_EQ(state.tier, 1);
	EXPECT_EQ(state.damagePercentForTarget(12), 0);
	state.recordAttack(12);
	EXPECT_EQ(state.targetUnitId, 12u);
	EXPECT_EQ(state.tier, 0);
	EXPECT_EQ(state.damagePercentForTarget(12), 0);
}

TEST(RelentlessAssaultState, AnActivationWithoutAnEligibleAttackBreaksTheChain)
{
	RelentlessAssaultState state;
	state.beginActivation();
	state.recordAttack(11);
	state.beginActivation();
	state.recordAttack(11);
	ASSERT_EQ(state.tier, 1);

	state.beginActivation();
	EXPECT_EQ(state.damagePercentForTarget(11), 20);
	state.beginActivation(); // The prior activation had no qualifying attack.
	EXPECT_EQ(state.targetUnitId, RelentlessAssaultState::INVALID_TARGET);
	EXPECT_EQ(state.tier, 0);
	EXPECT_EQ(state.damagePercentForTarget(11), 0);
}

TEST(RelentlessAssaultState, InvalidShapesAreRejectedAndValidStateCopiesByValue)
{
	RelentlessAssaultState state;
	state.beginActivation();
	state.recordAttack(11);
	state.beginActivation();
	state.recordAttack(11);
	EXPECT_NO_THROW(state.validateShape());
	EXPECT_EQ(state, RelentlessAssaultState(state));

	auto invalid = state;
	invalid.activationDamagePercent = 20;
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	invalid = state;
	invalid.targetUnitId = RelentlessAssaultState::INVALID_TARGET;
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
}
