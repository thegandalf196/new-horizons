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

TEST(NewHorizonsMovementRules, RapidEmbarkationRoundsFinalTenPercentCostUp)
{
	using newHorizonsMovement::rapidEmbarkationCost;
	EXPECT_EQ(rapidEmbarkationCost(200), 20);
	EXPECT_EQ(rapidEmbarkationCost(221), 23);
	EXPECT_EQ(rapidEmbarkationCost(1), 1);
	EXPECT_EQ(rapidEmbarkationCost(0), 0);
	EXPECT_EQ(rapidEmbarkationCost(std::numeric_limits<int>::max()), 214748365);
}

TEST(NewHorizonsMovementRules, PursuitMarchFloorsTenPercentAndCapsOnlyTheAddition)
{
	using newHorizonsMovement::pursuitMarchRestoration;
	EXPECT_EQ(pursuitMarchRestoration(100, 221), 22);
	EXPECT_EQ(pursuitMarchRestoration(219, 221), 2);
	EXPECT_EQ(pursuitMarchRestoration(221, 221), 0);
	EXPECT_EQ(pursuitMarchRestoration(300, 221), 0);
	EXPECT_EQ(pursuitMarchRestoration(0, 9), 0);
	EXPECT_EQ(pursuitMarchRestoration(0, 0), 0);
	EXPECT_EQ(pursuitMarchRestoration(0, std::numeric_limits<int>::max()), 214748364);
}

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

TEST(NewHorizonsMovementRules, PathfindingHalvesOnlyTerrainSurchargeBeforeFinalCeiling)
{
	using newHorizonsMovement::stepCost;
	EXPECT_EQ(stepCost(false, false, false, false, false, true), 12);
	EXPECT_EQ(stepCost(true, false, false, false, false, true), 17);
	EXPECT_EQ(stepCost(false, false, true, false, false, true), 14);
	EXPECT_EQ(stepCost(true, false, true, false, false, true), 20);
	EXPECT_EQ(stepCost(false, true, false, false, false, true), 10);
	EXPECT_EQ(stepCost(true, true, true, false, false, true), 14);
	EXPECT_EQ(stepCost(false, false, false, true, false, true), 9);
	EXPECT_EQ(stepCost(true, false, true, true, false, true), 14);
	EXPECT_EQ(stepCost(false, false, false, false, true, true), 18);
	EXPECT_EQ(stepCost(true, false, false, true, true, true), 17);
	EXPECT_EQ(stepCost(false, true, false, false, true, true), 15);
}

TEST(NewHorizonsMovementRules, SpecialTravelMultiplierIsAppliedBeforeFinalCeiling)
{
	using newHorizonsMovement::stepCost;
	EXPECT_EQ(stepCost(false, true, false, false, true), 15); // ceil(10 * 1.5)
	EXPECT_EQ(stepCost(true, true, false, false, true), 21); // ceil(14 * 1.5)
	EXPECT_EQ(stepCost(false, false, false, false, true), 21); // ceil(10 * 1.4 * 1.5)
	EXPECT_EQ(stepCost(true, false, false, false, true), 30); // ceil(14 * 1.4 * 1.5)
	EXPECT_EQ(stepCost(false, false, true, false, true), 27); // ceil(10 * 1.8 * 1.5)
	EXPECT_EQ(stepCost(true, false, true, false, true), 38); // ceil(14 * 1.8 * 1.5)
	EXPECT_EQ(stepCost(false, true, false, true, true), 11); // ceil(10 * .67 * 1.5)
	EXPECT_EQ(stepCost(false, false, false, true, true), 15); // ceil(10 * 1.4 * .67 * 1.5)
	EXPECT_EQ(stepCost(false, false, true, true, true), 19); // ceil(10 * 1.8 * .67 * 1.5)
	EXPECT_EQ(stepCost(true, false, false, true, true), 20); // ceil(14 * 1.4 * .67 * 1.5)
	EXPECT_EQ(stepCost(true, false, true, true, true), 26); // ceil(14 * 1.8 * .67 * 1.5)
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

TEST(NewHorizonsMovementRules, RoadmasterAddsAQuarterReductionToRoadCostsOnly)
{
	using newHorizonsMovement::stepCost;
	EXPECT_EQ(stepCost(false, true, false, false, false, false, true), 10);
	EXPECT_EQ(stepCost(true, true, false, false, false, false, true), 14);
	EXPECT_EQ(stepCost(false, true, false, true), 7);
	EXPECT_EQ(stepCost(false, true, false, true, false, false, true), 6);
	EXPECT_EQ(stepCost(true, true, false, true), 10);
	EXPECT_EQ(stepCost(true, true, false, true, false, false, true), 8);
	EXPECT_EQ(stepCost(false, false, false, true, false, false, true), 8);
}

TEST(NewHorizonsMovementRules, WayfarerCapsTerrainBeforeRoadAndTravelMultipliers)
{
	using newHorizonsMovement::stepCost;
	EXPECT_EQ(stepCost(false, false, false, false), 14);
	EXPECT_EQ(stepCost(true, false, false, false), 20);
	EXPECT_EQ(stepCost(false, false, false, false, false, false, false, true), 13);
	EXPECT_EQ(stepCost(true, false, true, false, false, false, false, true), 18);
	EXPECT_EQ(stepCost(false, false, true, false, false, false, false, true), 13);
	EXPECT_EQ(stepCost(false, false, true, false, false, true, false, true), 13);
	EXPECT_EQ(stepCost(false, false, false, false, false, true, false, true), 12);
	EXPECT_EQ(stepCost(true, false, false, false, false, true, false, true), 17);
	EXPECT_EQ(stepCost(true, true, true, false, false, false, false, true), 14);
	EXPECT_EQ(stepCost(false, false, true, true, false, false, false, true), 9);
	EXPECT_EQ(stepCost(false, false, true, false, true, false, false, true), 19);
}

TEST(NewHorizonsMovementRules, RoadmasterAndWayfarerComposeBeforeTheSingleFinalCeiling)
{
	EXPECT_EQ(newHorizonsMovement::stepCost(false, false, true, true, false, false, true, true), 7);
	EXPECT_EQ(newHorizonsMovement::stepCost(true, false, true, true, true, true, true, true), 14);
}

TEST(NewHorizonsMovementRules, ExtremePercentagesAreWidenedAndSafelyClamped)
{
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(-101), 0);
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(10001), 20202);
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(std::numeric_limits<std::int64_t>::max()),
		std::numeric_limits<int>::max());
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(std::numeric_limits<std::int64_t>::min()), 0);
}
