/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/GameConstants.h"
#include "../../lib/entities/hero/NewHorizonsCommandPlacement.h"
#include <limits>
#include <set>

using namespace newHorizonsHeroes;
using Category = newHorizonsCreatures::CreatureCategory;

TEST(NewHorizonsCommandPlacement, AssimilationBypassesOnlyTierPermissionIncludingAtCommandOne)
{
	const CommandCreatureTraits core{Category::CORE, false};
	const CommandCreatureTraits elite{Category::ELITE, false};
	const CommandCreatureTraits elemental{Category::ELITE, true};
	EXPECT_EQ(commandPlacementEligibility(1, SlotID(0), core), CommandPlacementError::NONE);
	EXPECT_EQ(commandPlacementEligibility(1, SlotID(0), elite), CommandPlacementError::TIER_NOT_PERMITTED);
	EXPECT_EQ(commandPlacementEligibility(1, SlotID(0), elemental), CommandPlacementError::NONE);
	EXPECT_EQ(commandPlacementEligibility(1, SlotID(3), elemental), CommandPlacementError::LOCKED_SLOT);
	EXPECT_EQ(commandPlacementEligibility(2, SlotID(3), elite), CommandPlacementError::NONE);
	EXPECT_EQ(commandPlacementEligibility(2, SlotID(3), core), CommandPlacementError::NONE);
	EXPECT_EQ(commandPlacementEligibility(2, SlotID(3), {Category::CHAMPION, false}), CommandPlacementError::TIER_NOT_PERMITTED);
	EXPECT_EQ(commandPlacementEligibility(5, SlotID(6), {Category::CHAMPION, false}), CommandPlacementError::NONE);
	EXPECT_EQ(commandPlacementEligibility(1, SlotID(0), {}), CommandPlacementError::UNKNOWN_CREATURE_TIER);
	EXPECT_EQ(commandPlacementEligibility(std::nullopt, SlotID(6), {}), CommandPlacementError::NONE);
	EXPECT_EQ(commandPlacementEligibility(std::nullopt, SlotID(7), core), CommandPlacementError::INVALID_SLOT);
}

TEST(NewHorizonsCommandPlacement, FindsCompleteArrangementInsteadOfTrustingFirstFreeSlot)
{
	const std::vector<CommandPlacementStack> input = {
		{CreatureID(1), 100, {Category::CORE, false}, SlotID(6)},
		{CreatureID(2), 100, {Category::CORE, false}, SlotID(3)},
		{CreatureID(3), 100, {Category::CORE, false}, SlotID(4)},
		{CreatureID(4), 100, {Category::ELITE, false}, SlotID(0)},
		{CreatureID(5), 100, {Category::ELITE, false}, SlotID(1)},
		{CreatureID(6), 100, {Category::ELITE, false}, SlotID(2)},
		{CreatureID(7), 100, {Category::CHAMPION, false}, SlotID()}};
	const auto plan = planCommandPlacement(5, input);
	ASSERT_TRUE(plan);
	ASSERT_EQ(plan->size(), 7u);
	EXPECT_EQ(plan->back().slot, SlotID(6));
	std::set<int> used;
	for(const auto & assignment : *plan)
	{
		EXPECT_TRUE(used.insert(assignment.slot.getNum()).second);
		EXPECT_EQ(commandPlacementEligibility(5, assignment.slot, input[assignment.inputIndex].traits), CommandPlacementError::NONE);
	}
	EXPECT_EQ(input.front().preferredSlot, SlotID(6));
	EXPECT_FALSE(planCommandPlacement(4, input));
}

TEST(NewHorizonsCommandPlacement, SevenStackConfluxArrangementAndRepeatedStacksPreserveIdentity)
{
	std::vector<CommandPlacementStack> input;
	input.push_back({CreatureID(1), 10, {Category::CORE, false}, SlotID()});
	input.push_back({CreatureID(2), 10, {Category::CORE, false}, SlotID()});
	for(int i = 0; i < 4; ++i)
		input.push_back({CreatureID(3 + i), 10, {Category::ELITE, true}, SlotID()});
	input.push_back({CreatureID(7), 10, {Category::CHAMPION, false}, SlotID()});
	const auto plan = planCommandPlacement(5, input);
	ASSERT_TRUE(plan);
	EXPECT_EQ(plan->back().slot, SlotID(6));
	const std::vector<CommandPlacementStack> repeated = {
		{CreatureID(1), std::numeric_limits<TQuantity>::max(), {Category::ELITE, true}, SlotID()},
		{CreatureID(1), 100000, {Category::ELITE, true}, SlotID()},
		{CreatureID(1), 100000, {Category::ELITE, true}, SlotID()}};
	ASSERT_TRUE(planCommandPlacement(1, repeated));
	EXPECT_EQ(planCommandPlacement(1, repeated)->size(), 3u);
	input = repeated;
	input.push_back(repeated.front());
	EXPECT_FALSE(planCommandPlacement(1, input));
	input.front().count = static_cast<int64_t>(std::numeric_limits<TQuantity>::max()) + 1;
	EXPECT_FALSE(planCommandPlacement(std::nullopt, input));
}
