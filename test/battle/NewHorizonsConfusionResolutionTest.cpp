/*
 * NewHorizonsConfusionResolutionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsConfusionResolution.h"

namespace
{
using namespace newHorizonsConfusion;
using Behavior = battle::ConfusionBehavior;

double weight(const std::vector<Outcome> & outcomes, Behavior behavior)
{
	double total = 0;
	for(const auto & outcome : outcomes)
		if(outcome.behavior == behavior)
			total += outcome.probability;
	return total;
}

Choices openChoices()
{
	Choices choices;
	AttackTargetChoices first;
	first.targetId = 1;
	first.attacks.push_back({{EActionType::WALK_AND_ATTACK, BattleHex(4, 5), nullptr}, false});
	AttackTargetChoices second;
	second.targetId = 2;
	second.attacks.push_back({{EActionType::SHOOT, BattleHex(5, 5), nullptr}, false});
	second.attacks.push_back({{EActionType::SHOOT, BattleHex(6, 5), nullptr}, true});
	choices.attacks = {first, second};
	choices.wanderDestinations = {BattleHex(7, 5), BattleHex(8, 5), BattleHex(9, 5)};
	return choices;
}
}

TEST(NewHorizonsConfusionResolutionTest, RawFamiliesStayEqualAndEnemyGroupsDoNotFlatten)
{
	const auto outcomes = enumerateOutcomes(openChoices(), Behavior::NONE, false);
	ASSERT_EQ(outcomes.size(), 7);
	EXPECT_NEAR(weight(outcomes, Behavior::ATTACK), 1.0 / 3, 1e-12);
	EXPECT_NEAR(weight(outcomes, Behavior::DEFEND), 1.0 / 3, 1e-12);
	EXPECT_NEAR(weight(outcomes, Behavior::WANDER), 1.0 / 3, 1e-12);
	EXPECT_NEAR(outcomes[0].probability, 1.0 / 6, 1e-12);
	EXPECT_NEAR(outcomes[1].probability, 1.0 / 12, 1e-12);
	EXPECT_NEAR(outcomes[2].probability, 1.0 / 12, 1e-12);
	EXPECT_TRUE(outcomes[2].action.skirmisher);
}

TEST(NewHorizonsConfusionResolutionTest, ImpossibleAttackAndTrappedWanderBothResolveAsDefend)
{
	Choices choices;
	choices.attacks.push_back({});
	const auto outcomes = enumerateOutcomes(choices, Behavior::NONE, false);
	EXPECT_NEAR(weight(outcomes, Behavior::DEFEND), 1.0, 1e-12);
	EXPECT_EQ(weight(outcomes, Behavior::ATTACK), 0);
	EXPECT_EQ(weight(outcomes, Behavior::WANDER), 0);
	for(const auto & outcome : outcomes)
		EXPECT_EQ(outcome.action.type, EActionType::DEFEND);
}

TEST(NewHorizonsConfusionResolutionTest, AdvanceKeepsAttackBehaviorAndUniformTargetProbability)
{
	Choices choices;
	AttackTargetChoices advance;
	advance.furthestAdvances.push_back(BattleHex(8, 5));
	choices.attacks = {advance, {}};
	const auto outcomes = enumerateOutcomes(choices, Behavior::NONE, false);
	EXPECT_NEAR(weight(outcomes, Behavior::ATTACK), 1.0 / 6, 1e-12);
	EXPECT_NEAR(weight(outcomes, Behavior::DEFEND), 5.0 / 6, 1e-12);
	EXPECT_EQ(outcomes.front().action.type, EActionType::WALK);
	EXPECT_EQ(outcomes.front().action.position, BattleHex(8, 5));
}

TEST(NewHorizonsConfusionResolutionTest, ConfounderConditionsResolvedBehaviorNotRawFamily)
{
	auto choices = openChoices();
	choices.attacks.push_back({}); // One third of Attack resolves as Defend.
	choices.wanderDestinations.clear(); // All Wander resolves as Defend.
	const auto outcomes = enumerateOutcomes(choices, Behavior::DEFEND, true);
	ASSERT_EQ(outcomes.size(), 3);
	EXPECT_NEAR(weight(outcomes, Behavior::ATTACK), 1.0, 1e-12);
	EXPECT_EQ(weight(outcomes, Behavior::DEFEND), 0);
	EXPECT_NEAR(outcomes[0].probability, 0.5, 1e-12);
	EXPECT_NEAR(outcomes[1].probability, 0.25, 1e-12);
}

TEST(NewHorizonsConfusionResolutionTest, ConfounderAllowsSoleResolvedBehaviorToRepeat)
{
	const auto outcomes = enumerateOutcomes(Choices{}, Behavior::DEFEND, true);
	ASSERT_FALSE(outcomes.empty());
	EXPECT_NEAR(weight(outcomes, Behavior::DEFEND), 1.0, 1e-12);
	EXPECT_EQ(selectOutcome(outcomes, 0.0).behavior, Behavior::DEFEND);
	EXPECT_EQ(selectOutcome(outcomes, 0.999999).behavior, Behavior::DEFEND);
}

TEST(NewHorizonsConfusionResolutionTest, SelectionUsesCumulativeWeightsAndRejectsInvalidDraws)
{
	const auto outcomes = enumerateOutcomes(openChoices(), Behavior::NONE, false);
	EXPECT_EQ(&selectOutcome(outcomes, 0.0), &outcomes[0]);
	EXPECT_EQ(&selectOutcome(outcomes, 0.2), &outcomes[1]);
	EXPECT_EQ(selectOutcome(outcomes, 0.5).behavior, Behavior::DEFEND);
	EXPECT_EQ(selectOutcome(outcomes, 0.9).behavior, Behavior::WANDER);
	EXPECT_THROW(selectOutcome(outcomes, 1.0), std::invalid_argument);
	EXPECT_THROW(selectOutcome(outcomes, -0.1), std::invalid_argument);
	EXPECT_THROW(selectOutcome({}, 0.0), std::invalid_argument);
}
