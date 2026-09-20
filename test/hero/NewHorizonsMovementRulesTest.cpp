/*
 * NewHorizonsMovementRulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"

#include <cstdint>
#include <limits>

TEST(NewHorizonsMovementRules, DailyMovementUsesBasePercentageAndFlatBonuses)
{
	using newHorizonsMovement::maximumDailyMovement;
	EXPECT_EQ(maximumDailyMovement(0), 200);
	EXPECT_EQ(maximumDailyMovement(10), 220);
	EXPECT_EQ(maximumDailyMovement(20), 240);
	EXPECT_EQ(maximumDailyMovement(30), 260);
	EXPECT_EQ(maximumDailyMovement(1), 202);
	EXPECT_EQ(maximumDailyMovement(10, 3), 223);
	EXPECT_EQ(maximumDailyMovement(200, 10, 10, 3), 245);
	EXPECT_EQ(maximumDailyMovement(-100, 17), 17);
	EXPECT_EQ(maximumDailyMovement(0, std::numeric_limits<int>::max()), std::numeric_limits<int>::max());
}

TEST(NewHorizonsMovementRules, StepCostUsesCanonicalMultipliersAndCeiling)
{
	using newHorizonsMovement::stepCost;
	EXPECT_EQ(stepCost(false, true, false, false), 10);
	EXPECT_EQ(stepCost(true, true, false, false), 14);
	EXPECT_EQ(stepCost(false, false, false, false), 14); // ceil(10 * 1.40)
	EXPECT_EQ(stepCost(true, false, false, false), 20); // ceil(14 * 1.40)
	EXPECT_EQ(stepCost(false, false, true, false), 18); // ceil(10 * 1.80)
	EXPECT_EQ(stepCost(true, false, true, false), 26); // ceil(14 * 1.80)
	EXPECT_EQ(stepCost(false, true, false, true), 7); // ceil(10 * .67)
	EXPECT_EQ(stepCost(false, false, false, true), 10); // ceil(10 * 1.40 * .67)
	EXPECT_EQ(stepCost(false, false, true, true), 13); // ceil(10 * 1.80 * .67)
	EXPECT_EQ(stepCost(true, false, false, true), 14); // ceil(14 * 1.40 * .67)
	EXPECT_EQ(stepCost(true, false, true, true), 17); // ceil(14 * 1.80 * .67)
}

TEST(NewHorizonsMovementRules, NativeAffinityOverridesNonNativeAndDesertSurcharges)
{
	using newHorizonsMovement::stepCost;
	// The caller supplies the canonical affinity decision: hero-native OR an
	// army composed entirely of native stacks.  Mixed/non-native armies do not.
	EXPECT_EQ(stepCost(false, true, false, false), 10);
	EXPECT_EQ(stepCost(false, true, true, false), 10);
	EXPECT_EQ(stepCost(false, false, false, false), 14);
	EXPECT_EQ(stepCost(false, false, true, false), 18);
}

TEST(NewHorizonsMovementRules, ExtremePercentagesAreWidenedAndSafelyClamped)
{
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(-101), 0);
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(10001), 20202);
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(std::numeric_limits<std::int64_t>::max()),
		std::numeric_limits<int>::max());
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(std::numeric_limits<std::int64_t>::min()), 0);
}
