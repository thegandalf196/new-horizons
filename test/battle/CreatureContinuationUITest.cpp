/*
 * CreatureContinuationUITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../client/battle/CreatureContinuationUI.h"

namespace
{
using creatureContinuationUI::Kind;
using creatureContinuationUI::State;
using Action = PossiblePlayerBattleAction;

TEST(CreatureContinuationUI, OrdinaryStateLeavesCommandsAndCreatureActionsUnchanged)
{
	const State state;
	EXPECT_FALSE(state.pending());
	for(const auto action : {Action::MOVE_STACK, Action::ATTACK, Action::SHOOT, Action::NO_LOCATION})
		EXPECT_TRUE(state.allows(action));
	for(auto action : {EActionType::DEFEND, EActionType::WAIT, EActionType::WALK, EActionType::MONSTER_SPELL})
	{
		const auto original = action;
		EXPECT_TRUE(state.normalizeCommand(action));
		EXPECT_EQ(action, original);
	}
}

TEST(CreatureContinuationUI, DreadChargeAtZeroMovementRetainsMeleeAndDeclineOnly)
{
	const State state{Kind::DREAD_CAVALRY, true, 50, 0};
	EXPECT_TRUE(state.pending());
	EXPECT_TRUE(state.allows(Action::ATTACK));
	EXPECT_TRUE(state.allows(Action::WALK_AND_ATTACK));
	for(const auto action : {Action::MOVE_STACK, Action::SHOOT, Action::NO_LOCATION, Action::ATTACK_AND_RETURN})
		EXPECT_FALSE(state.allows(action));
	auto command = EActionType::WALK_AND_ATTACK;
	EXPECT_TRUE(state.normalizeCommand(command));
	command = EActionType::DEFEND;
	EXPECT_TRUE(state.normalizeCommand(command));
	EXPECT_EQ(command, EActionType::NO_ACTION);
	EXPECT_EQ(state.remainingMovement, 0);
}

TEST(CreatureContinuationUI, StalePendingStateKeepsInformationAndSkipButNoAttack)
{
	const State state{Kind::DREAD_CAVALRY, false, 100, 0};
	EXPECT_TRUE(state.allows(Action::HERO_INFO));
	EXPECT_TRUE(state.allows(Action::CREATURE_INFO));
	EXPECT_FALSE(state.allows(Action::ATTACK));
	auto command = EActionType::WALK_AND_ATTACK;
	EXPECT_FALSE(state.normalizeCommand(command));
	command = EActionType::DEFEND;
	EXPECT_TRUE(state.normalizeCommand(command));
	EXPECT_EQ(command, EActionType::NO_ACTION);
}

TEST(CreatureContinuationUI, PendingWaitAndIndependentMovementCannotSpendTheCharge)
{
	const State state{Kind::DREAD_CAVALRY, true, 50, 3};
	for(auto action : {EActionType::WAIT, EActionType::WALK, EActionType::MONSTER_SPELL, EActionType::DEMONIC_GATING})
		EXPECT_FALSE(state.normalizeCommand(action));
	for(auto action : {EActionType::HERO_SPELL, EActionType::HERO_COMMAND, EActionType::RETREAT, EActionType::SURRENDER})
		EXPECT_TRUE(state.normalizeCommand(action));
}

TEST(CreatureContinuationUI, MasterGunnerPolicyRetainsShootInformationAndNoActionDecline)
{
	const State state{Kind::MASTER_GUNNER, true, 100, 0};
	EXPECT_TRUE(state.allows(Action::SHOOT));
	EXPECT_TRUE(state.allows(Action::CREATURE_INFO));
	EXPECT_FALSE(state.allows(Action::ATTACK));
	EXPECT_FALSE(state.allows(Action::MOVE_STACK));
	auto command = EActionType::SHOOT;
	EXPECT_TRUE(state.normalizeCommand(command));
	command = EActionType::DEFEND;
	EXPECT_TRUE(state.normalizeCommand(command));
	EXPECT_EQ(command, EActionType::NO_ACTION);
	command = EActionType::WAIT;
	EXPECT_FALSE(state.normalizeCommand(command));
}
}
