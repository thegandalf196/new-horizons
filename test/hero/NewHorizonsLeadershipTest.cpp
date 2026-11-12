/*
 * NewHorizonsLeadershipTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"

#include <limits>
#include <stdexcept>

TEST(NewHorizonsLeadership, ClassLevelAndSkillContributeWithoutPrimaryRatings)
{
	using newHorizonsHeroes::leadershipCapacity;
	EXPECT_EQ(leadershipCapacity(2000, 200, 1, 0, 0, 50).capacity, 2000);
	EXPECT_EQ(leadershipCapacity(2000, 200, 4, 0, 0, 50).capacity, 2600);
	EXPECT_EQ(leadershipCapacity(2000, 200, 4, 30, 0, 50).capacity, 3380);
	EXPECT_EQ(leadershipCapacity(1500, 150, 4, 30, 0, 50).capacity, 2535);
}

TEST(NewHorizonsLeadership, SoftCapacityNeverChangesUsageAndBoundsMovement)
{
	using newHorizonsHeroes::leadershipCapacity;
	for(uint64_t used : {uint64_t(0), uint64_t(1999), uint64_t(2000)})
	{
		const auto view = leadershipCapacity(2000, 200, 1, 0, used, 50);
		EXPECT_FALSE(view.overCapacity());
		EXPECT_EQ(view.used, used);
		EXPECT_EQ(view.movementPercent, 100);
	}
	const auto overloaded = leadershipCapacity(2000, 200, 1, 0, 2500, 50);
	EXPECT_TRUE(overloaded.overCapacity());
	EXPECT_EQ(overloaded.used, 2500);
	EXPECT_EQ(overloaded.movementPercent, 80);
	const auto extreme = leadershipCapacity(2000, 200, 1, 0,
		std::numeric_limits<uint64_t>::max(), 50);
	EXPECT_EQ(extreme.used, std::numeric_limits<uint64_t>::max());
	EXPECT_EQ(extreme.movementPercent, 50);
}

TEST(NewHorizonsLeadership, MovementScalingPreservesNeutralAndUsesWideIntermediates)
{
	using newHorizonsHeroes::leadershipMovement;
	EXPECT_EQ(leadershipMovement(1500, 100), 1500);
	EXPECT_EQ(leadershipMovement(1500, 80), 1200);
	EXPECT_EQ(leadershipMovement(1501, 50), 750);
	EXPECT_EQ(leadershipMovement(0, 50), 0);
	EXPECT_EQ(leadershipMovement(std::numeric_limits<int>::max(), 100),
		std::numeric_limits<int>::max());
	EXPECT_THROW(leadershipMovement(-1, 50), std::invalid_argument);
	EXPECT_THROW(leadershipMovement(1500, 0), std::invalid_argument);
	EXPECT_THROW(leadershipMovement(1500, 101), std::invalid_argument);
}

TEST(NewHorizonsLeadership, WideInputsAndInvalidRules)
{
	using newHorizonsHeroes::leadershipCapacity;
	EXPECT_EQ(leadershipCapacity(1000000, 1000000, 1000000, 1000, 0, 50).capacity,
		int64_t(11000000000000));
	EXPECT_THROW(leadershipCapacity(0, 0, 1, 0, 0, 50), std::invalid_argument);
	EXPECT_THROW(leadershipCapacity(1, -1, 1, 0, 0, 50), std::invalid_argument);
	EXPECT_THROW(leadershipCapacity(1, 0, 0, 0, 0, 50), std::invalid_argument);
	EXPECT_THROW(leadershipCapacity(1, 0, 1, -1, 0, 50), std::invalid_argument);
	EXPECT_THROW(leadershipCapacity(1, 0, 1, 1001, 0, 50), std::invalid_argument);
	EXPECT_THROW(leadershipCapacity(1, 0, 1, 0, 0, 0), std::invalid_argument);
	EXPECT_THROW(leadershipCapacity(1, 0, 1, 0, 0, 101), std::invalid_argument);
}

TEST(NewHorizonsLeadership, SquareRootEfficiencyAndFullCapacity)
{
	using newHorizonsHeroes::computeFullEfficiencyCapacity;
	using newHorizonsHeroes::squareRootLeadershipEfficiency;

	// C = max(1, floor(L / R))
	EXPECT_EQ(computeFullEfficiencyCapacity(650, 10), 65);
	EXPECT_EQ(computeFullEfficiencyCapacity(500, 1000), 1); // floor is 0, clamped to 1
	EXPECT_EQ(computeFullEfficiencyCapacity(0, 10), 1);
	EXPECT_EQ(computeFullEfficiencyCapacity(1200, 0), 1200);

	// Within capacity: exactly 1.0
	EXPECT_DOUBLE_EQ(squareRootLeadershipEfficiency(10, 65), 1.0);
	EXPECT_DOUBLE_EQ(squareRootLeadershipEfficiency(65, 65), 1.0);
	EXPECT_DOUBLE_EQ(squareRootLeadershipEfficiency(0, 65), 1.0);

	// Over capacity: N_eff = C + (N - C) * sqrt(C / N), eff = N_eff / N
	// For C = 100, N = 400: sqrt(100/400) = 0.5
	// N_eff = 100 + 300 * 0.5 = 250
	// eff = 250 / 400 = 0.625
	EXPECT_DOUBLE_EQ(squareRootLeadershipEfficiency(400, 100), 0.625);

	// Monotonicity: total effective creatures (damage) strictly increases with count
	double prevEffective = 0.0;
	const uint64_t capacity = 50;
	for(uint64_t count = 1; count <= 500; ++count)
	{
		const double eff = squareRootLeadershipEfficiency(count, capacity);
		const double effectiveCount = count * eff;
		EXPECT_GT(effectiveCount, prevEffective);
		EXPECT_LE(eff, 1.0);
		EXPECT_GT(eff, 0.0);
		prevEffective = effectiveCount;
	}
}

