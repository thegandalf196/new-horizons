/*
 * NewHorizonsMusterTest.cpp, part of VCMI engine
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Helpers/NewHorizonsMuster.h"

#include <vector>

using newHorizonsCreatures::CreatureCategory;
using NK2AI::newHorizonsMuster::amountMultiplier;
using NK2AI::newHorizonsMuster::chooseBroadMusterSplit;
using NK2AI::newHorizonsMuster::externalAmountMultiplier;

namespace
{
using ::newHorizonsMuster::PerkModifiers;
using NK2AI::newHorizonsMuster::CoreMusterRow;
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, basicOnlyTargetsCoreWithExactAmount)
{
	EXPECT_EQ(amountMultiplier(1, CreatureCategory::CORE), std::optional<int>(2));
	EXPECT_FALSE(amountMultiplier(1, CreatureCategory::ELITE));
	EXPECT_FALSE(amountMultiplier(1, CreatureCategory::CHAMPION));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, advancedUsesFourCoreOrOneElite)
{
	EXPECT_EQ(amountMultiplier(2, CreatureCategory::CORE), std::optional<int>(4));
	EXPECT_EQ(amountMultiplier(2, CreatureCategory::ELITE), std::optional<int>(1));
	EXPECT_FALSE(amountMultiplier(2, CreatureCategory::CHAMPION));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, expertUsesSixCoreTwoEliteOrOneChampion)
{
	EXPECT_EQ(amountMultiplier(3, CreatureCategory::CORE), std::optional<int>(6));
	EXPECT_EQ(amountMultiplier(3, CreatureCategory::ELITE), std::optional<int>(2));
	EXPECT_EQ(amountMultiplier(3, CreatureCategory::CHAMPION), std::optional<int>(1));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, invalidRanksAndUnknownCategoryAreRejected)
{
	EXPECT_FALSE(amountMultiplier(0, CreatureCategory::CORE));
	EXPECT_FALSE(amountMultiplier(4, CreatureCategory::CORE));
	EXPECT_FALSE(amountMultiplier(3, static_cast<CreatureCategory>(255)));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, recruitmentPerksModifyOnlyTheirTargetCategory)
{
	PerkModifiers modifiers;
	modifiers.volunteerNetwork = true;
	modifiers.eliteDraft = true;
	modifiers.championsCall = true;

	EXPECT_EQ(amountMultiplier(1, CreatureCategory::CORE, modifiers), std::optional<int>(4));
	EXPECT_EQ(amountMultiplier(2, CreatureCategory::CORE, modifiers), std::optional<int>(6));
	EXPECT_EQ(amountMultiplier(2, CreatureCategory::ELITE, modifiers), std::optional<int>(2));
	EXPECT_EQ(amountMultiplier(3, CreatureCategory::CHAMPION, modifiers), std::optional<int>(2));
	EXPECT_FALSE(amountMultiplier(1, CreatureCategory::ELITE, modifiers));
	EXPECT_FALSE(amountMultiplier(2, CreatureCategory::CHAMPION, modifiers));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, masterRecruiterDoublesUsesWithoutChangingTargetRule)
{
	PerkModifiers modifiers;
	EXPECT_EQ(::newHorizonsMuster::maximumUsesPerWeek(modifiers), 1);
	modifiers.masterRecruiter = true;
	EXPECT_EQ(::newHorizonsMuster::maximumUsesPerWeek(modifiers), 2);
	EXPECT_EQ(::newHorizonsMuster::absoluteWeek(0, 7), 0);
	EXPECT_EQ(::newHorizonsMuster::absoluteWeek(1, 7), 0);
	EXPECT_EQ(::newHorizonsMuster::absoluteWeek(8, 7), 1);
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, externalRecruiterAddsFixedTwoOnlyForCoreAndValidRank)
{
	EXPECT_EQ(externalAmountMultiplier(1, CreatureCategory::CORE, true), std::optional<int>(2));
	EXPECT_EQ(externalAmountMultiplier(3, CreatureCategory::CORE, true), std::optional<int>(2));
	EXPECT_FALSE(externalAmountMultiplier(0, CreatureCategory::CORE, true));
	EXPECT_FALSE(externalAmountMultiplier(4, CreatureCategory::CORE, true));
	EXPECT_FALSE(externalAmountMultiplier(1, CreatureCategory::CORE, false));
	EXPECT_FALSE(externalAmountMultiplier(1, CreatureCategory::ELITE, true));
	EXPECT_FALSE(externalAmountMultiplier(1, CreatureCategory::CHAMPION, true));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, broadMusterRequiresActivePerkTwoCoreRowsAndPositiveSplitTotal)
{
	const std::vector<CoreMusterRow> noCoreRows;
	const std::vector<CoreMusterRow> oneCoreRow = {
		{CreatureID(1), 0, 100, 0, std::nullopt}
	};
	const std::vector<CoreMusterRow> twoCoreRows = {
		{CreatureID(1), 0, 100, 0, std::nullopt},
		{CreatureID(2), 1, 70, 0, std::nullopt}
	};

	EXPECT_FALSE(chooseBroadMusterSplit(twoCoreRows, 2, false, 0));
	EXPECT_FALSE(chooseBroadMusterSplit(noCoreRows, 2, true, 0));
	EXPECT_FALSE(chooseBroadMusterSplit(oneCoreRow, 2, true, 0));
	EXPECT_FALSE(chooseBroadMusterSplit(twoCoreRows, 0, true, 0));
	EXPECT_FALSE(chooseBroadMusterSplit(twoCoreRows, 1, true, 0));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, broadMusterRejectsAliasedRowsAndCreatureTargets)
{
	const std::vector<CoreMusterRow> sameRow = {
		{CreatureID(1), 0, 100, 0, std::nullopt},
		{CreatureID(2), 0, 70, 0, std::nullopt}
	};
	const std::vector<CoreMusterRow> sameCreature = {
		{CreatureID(1), 0, 100, 0, std::nullopt},
		{CreatureID(1), 1, 70, 0, std::nullopt}
	};

	EXPECT_FALSE(chooseBroadMusterSplit(sameRow, 2, true, 0));
	EXPECT_FALSE(chooseBroadMusterSplit(sameCreature, 2, true, 0));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, broadMusterSplitsOnlyWhenLeadershipAdmissionImprovesValue)
{
	const std::vector<CoreMusterRow> rows = {
		{CreatureID(1), 0, 100, 0, 1}, // This hero can admit only one of this Core creature.
		{CreatureID(2), 1, 70, 0, std::nullopt}
	};

	const auto split = chooseBroadMusterSplit(rows, 2, true, 140);
	ASSERT_TRUE(split);
	EXPECT_TRUE(split->valid());
	EXPECT_TRUE(split->isSplit());
	EXPECT_EQ(split->creature, CreatureID(1));
	EXPECT_EQ(split->secondCreature, CreatureID(2));
	EXPECT_EQ(split->row, 0);
	EXPECT_EQ(split->secondRow, 1);
	EXPECT_EQ(split->amount, 2);
	EXPECT_EQ(split->firstAmount, 1);
	EXPECT_EQ(split->secondAmount, 1);
	EXPECT_EQ(split->firstAmount + split->secondAmount, split->amount);
	EXPECT_EQ(split->armyValue, 170);
	EXPECT_EQ(split->recruitableArmyValue, 170);

	// A tie with the best single-target value stays on the existing solo path.
	EXPECT_FALSE(chooseBroadMusterSplit(rows, 2, true, 170));
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, broadMusterUsesExactTotalForLargerRankAmount)
{
	const std::vector<CoreMusterRow> rows = {
		{CreatureID(1), 0, 100, 1, 3}, // Two more can join the current stack.
		{CreatureID(2), 1, 70, 0, std::nullopt}
	};

	const auto split = chooseBroadMusterSplit(rows, 4, true, 280);
	ASSERT_TRUE(split);
	EXPECT_EQ(split->amount, 4);
	EXPECT_EQ(split->firstAmount, 2);
	EXPECT_EQ(split->secondAmount, 2);
	EXPECT_EQ(split->firstAmount + split->secondAmount, 4);
	EXPECT_EQ(split->recruitableArmyValue, 340);
}

TEST(Nullkiller2_Helpers_NewHorizonsMuster, broadMusterDoesNotSplitWithoutLeadershipValueGain)
{
	const std::vector<CoreMusterRow> rows = {
		{CreatureID(1), 0, 100, 0, std::nullopt},
		{CreatureID(2), 1, 70, 0, std::nullopt}
	};

	EXPECT_FALSE(chooseBroadMusterSplit(rows, 2, true, 200));
}
