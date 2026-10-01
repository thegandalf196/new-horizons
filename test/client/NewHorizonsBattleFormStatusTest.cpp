/*
 * NewHorizonsBattleFormStatusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"

namespace
{
using namespace newHorizonsBattleStatus;

BattleFormStatus activeForm(int32_t currentCreature = 8, int32_t originalCreature = 3,
	int32_t remainingRounds = 2, int64_t aggregateCreatureHealth = 487)
{
	return makeBattleFormStatus(true, currentCreature, originalCreature, remainingRounds,
		aggregateCreatureHealth, "Silver Pegasus", "Battle Dwarf");
}
}

TEST(NewHorizonsBattleFormStatusTest, ActiveSnapshotTracksCurrentAndOriginalSpeciesAndDuration)
{
	const auto status = activeForm();
	ASSERT_TRUE(status.active());
	EXPECT_EQ(status.currentCreature, 8);
	EXPECT_EQ(status.originalCreature, 3);
	EXPECT_EQ(status.remainingRounds, 2);
	EXPECT_EQ(status.aggregateCreatureHealth, 487);

	StackInfoStatusSnapshot snapshot;
	snapshot.battleForm = status;
	StackInfoStatusSnapshot changedSource;
	changedSource.battleForm = activeForm(8, 4, 2);
	StackInfoStatusSnapshot changedForm;
	changedForm.battleForm = activeForm(9, 3, 2);
	StackInfoStatusSnapshot changedDuration;
	changedDuration.battleForm = activeForm(8, 3, 1);
	EXPECT_NE(snapshot, changedSource);
	EXPECT_NE(snapshot, changedForm);
	EXPECT_NE(snapshot, changedDuration);
}

TEST(NewHorizonsBattleFormStatusTest, ExpiredOrInactiveFormHasNoDisplayedStatus)
{
	EXPECT_FALSE(makeBattleFormStatus(false, 8, 3, 2, 487, "Silver Pegasus", "Battle Dwarf").active());
	EXPECT_FALSE(makeBattleFormStatus(true, 8, 3, 0, 487, "Silver Pegasus", "Battle Dwarf").active());
	EXPECT_EQ(makeBattleFormStatus(true, 8, 3, 0, 487, "Silver Pegasus", "Battle Dwarf"), BattleFormStatus{});
}

TEST(NewHorizonsBattleFormStatusTest, TooltipExplainsFormHealthAndAllegiance)
{
	const auto tooltip = battleFormTooltip(activeForm());
	EXPECT_NE(tooltip.find("Current form: Silver Pegasus; original creature: Battle Dwarf."), std::string::npos);
	EXPECT_NE(tooltip.find("Current aggregate creature HP: 487 HP"), std::string::npos);
	EXPECT_NE(tooltip.find("preserve this exact surviving creature-HP total"), std::string::npos);
	EXPECT_NE(tooltip.find("Temporary HP is tracked separately"), std::string::npos);
	EXPECT_NE(tooltip.find("owner and battle-side allegiance are unchanged"), std::string::npos);
	EXPECT_NE(tooltip.find("2 rounds remaining"), std::string::npos);
	EXPECT_TRUE(battleFormTooltip({}).empty());
}

TEST(NewHorizonsBattleFormStatusTest, BattleFormUsesAPrioritizedCompactStatusSlot)
{
	EXPECT_LT(stackStatusPriority(StackStatusIconKind::BATTLE_FORM),
		stackStatusPriority(StackStatusIconKind::TIME_STOP));

	const auto plan = stackStatusDisplayPlan({StackStatusIconKind::ORDINARY,
		StackStatusIconKind::TIME_STOP, StackStatusIconKind::BATTLE_FORM}, 4);
	ASSERT_TRUE(plan.overflow);
	ASSERT_TRUE(plan.ellipsisUsesSlot);
	ASSERT_EQ(plan.visibleEntryIndices.size(), 2);
	EXPECT_EQ(plan.visibleEntryIndices[0], 2);
	EXPECT_EQ(plan.visibleEntryIndices[1], 1);
}
