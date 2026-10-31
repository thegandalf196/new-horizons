/*
 * NewHorizonsTrollSummonPowerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsTrollSummonPower.h"

#include <array>
#include <limits>

TEST(NewHorizonsTrollSummonPowerTest, SchoolCoefficientScalesOnlyThePowerTerm)
{
	const std::array<int32_t, 4> coefficients{10000, 11500, 13000, 14500};
	const std::array<int64_t, 4> expected{202, 217, 233, 248};
	for(size_t index = 0; index < coefficients.size(); ++index)
		EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(41, coefficients[index], 0, 0, false), expected[index]);
}

TEST(NewHorizonsTrollSummonPowerTest, BeastcallerScalesTheUnroundedWholePool)
{
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(0, 10000, 0, 0, false), 100);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(0, 10000, 0, 0, true), 125);
	const auto ordinary = newHorizonsMagic::trollSummonHitPoints(1, 11500, 0, 0, false);
	const auto enhanced = newHorizonsMagic::trollSummonHitPoints(1, 11500, 0, 0, true);
	EXPECT_EQ(ordinary, 102);
	EXPECT_EQ(enhanced, 128);
	EXPECT_NE(enhanced, ordinary * 125 / 100);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(41, 14500, 0, 0, true), 310);
}

TEST(NewHorizonsTrollSummonPowerTest, ExplicitCastCoefficientAndBonusesKeepBothFloors)
{
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(20, 12650, 20, 25, false), 194);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(20, 12650, 20, 25, true), 243);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(3, 14599, 0, 0, false), 110);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(3, 14667, 0, 0, false), 111);
}

TEST(NewHorizonsTrollSummonPowerTest, NegativeAndZeroPowerPreserveTheFixedPool)
{
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(-1, 10000, 0, 0, false), 100);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(std::numeric_limits<int32_t>::min(), 10000, 0, 0, true), 125);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(0, 100000, 1000, 1000, false), 100);
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(100, 0, 1000, 1000, true), 125);
}

TEST(NewHorizonsTrollSummonPowerTest, LargeValidPoolIsNotClippedToCreatureCountOrInt32)
{
	EXPECT_EQ(newHorizonsMagic::trollSummonHitPoints(std::numeric_limits<int32_t>::max(),
		100000, 1000, 1000, true), 8120172540343LL);
}

TEST(NewHorizonsTrollSummonPowerTest, InvalidScalerDomainsRemainRejected)
{
	for(const int32_t coefficient : {-1, 100001})
		EXPECT_THROW(newHorizonsMagic::trollSummonHitPoints(0, coefficient, 0, 0, false), std::invalid_argument);
	for(const int32_t bonus : {-1, 1001})
	{
		EXPECT_THROW(newHorizonsMagic::trollSummonHitPoints(0, 10000, bonus, 0, false), std::invalid_argument);
		EXPECT_THROW(newHorizonsMagic::trollSummonHitPoints(0, 10000, 0, bonus, false), std::invalid_argument);
	}
}
