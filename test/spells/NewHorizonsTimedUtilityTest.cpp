/*
 * NewHorizonsTimedUtilityTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsTimedUtility.h"
#include <limits>

TEST(NewHorizonsTimedUtilityTest, HolyArmorPreservesConfiguredBaseAndSinglePowerFloor)
{
	using newHorizonsMagic::holyArmorReduction;
	EXPECT_EQ(holyArmorReduction(30, 0, 12650, 10, 25, true, 20), 30);
	EXPECT_EQ(holyArmorReduction(25, 0, 12650, 10, 25, true, 20), 25);
	// floor(14 / 5 * 1.265 * 1.10 * 1.25 * 1.20 * 1.20) = 7.
	EXPECT_EQ(holyArmorReduction(30, 14, 12650, 10, 25, true, 20), 37);
	EXPECT_EQ(holyArmorReduction(25, 14, 12650, 10, 25, true, 20), 32);
	EXPECT_EQ(holyArmorReduction(30, 1000, 12650, 10, 25, true, 20), 60);
	EXPECT_EQ(holyArmorReduction(30, 4, 10000, 0, 0, false), 30);
	EXPECT_EQ(holyArmorReduction(30, 5, 10000, 0, 0, false), 31);
	EXPECT_THROW(holyArmorReduction(30, 10, 10000, 0, 0, false, -1), std::invalid_argument);
}

TEST(NewHorizonsTimedUtilityTest, ForgetfulnessCapsBeforeExtendAndEchoedDuration)
{
	using newHorizonsMagic::forgetfulnessBaseDuration;
	using newHorizonsMagic::durationWithCastExtensions;
	EXPECT_EQ(forgetfulnessBaseDuration(79, 10000, 0, 0), 1);
	EXPECT_EQ(forgetfulnessBaseDuration(80, 10000, 0, 0), 2);
	EXPECT_EQ(forgetfulnessBaseDuration(160, 10000, 0, 0), 3);
	EXPECT_EQ(forgetfulnessBaseDuration(1000, 10000, 0, 0), 3);
	EXPECT_EQ(forgetfulnessBaseDuration(51, 12650, 0, 25), 2);
	EXPECT_EQ(durationWithCastExtensions(forgetfulnessBaseDuration(1000, 10000, 0, 0), true, true), 5);
	EXPECT_EQ(durationWithCastExtensions(0, true, false), 0);
	EXPECT_EQ(durationWithCastExtensions(0, true, true), 1);
	EXPECT_EQ(durationWithCastExtensions(std::numeric_limits<int32_t>::max() - 1, true, true),
		std::numeric_limits<int32_t>::max());
	EXPECT_EQ(durationWithCastExtensions(std::numeric_limits<int32_t>::max(), true, true),
		std::numeric_limits<int32_t>::max());
}

TEST(TimedUtility, HeavenlyGaleKeepsFixedReductionOutsideAegisAndRecipientPowerRatios)
{
	EXPECT_EQ(newHorizonsMagic::heavenlyGaleReductionBasisPoints(0, 14500, 20, 30, true, 25), 5000);
	EXPECT_EQ(newHorizonsMagic::heavenlyGaleReductionBasisPoints(1, 10000, 0, 0, true, 25), 5022);
	EXPECT_EQ(newHorizonsMagic::heavenlyGaleReductionBasisPoints(10000, 14500, 20, 30, true, 25), 8000);
}
