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
