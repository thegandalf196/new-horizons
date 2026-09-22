/*
 * NewHorizonsMusterTest.cpp, part of VCMI engine
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Helpers/NewHorizonsMuster.h"

using newHorizonsCreatures::CreatureCategory;
using NK2AI::newHorizonsMuster::amountMultiplier;

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
