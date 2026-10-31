/*
 * NewHorizonsElementalConvergencePowerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsElementalConvergencePower.h"

#include <array>
#include <limits>

TEST(NewHorizonsElementalConvergencePowerTest, SchoolCoefficientScalesOnlyThePowerComponent)
{
	const std::array<int32_t, 4> coefficients{10000, 11500, 13000, 14500};
	const std::array<int64_t, 4> expected{455, 485, 516, 547};
	for(size_t index = 0; index < coefficients.size(); ++index)
		EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(41, coefficients[index], 0, 0, false), expected[index]);
}

TEST(NewHorizonsElementalConvergencePowerTest, ConjurerScalesBothPartsBeforeWholeHPRounding)
{
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(0, 10000, 0, 0, false), 250);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(0, 10000, 0, 0, true), 325);
	const auto ordinary = newHorizonsMagic::elementalConvergenceHitPoints(1, 11500, 0, 0, false);
	const auto enhanced = newHorizonsMagic::elementalConvergenceHitPoints(1, 11500, 0, 0, true);
	EXPECT_EQ(ordinary, 255);
	EXPECT_EQ(enhanced, 332);
	EXPECT_NE(enhanced, ordinary * 130 / 100);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(41, 14500, 0, 0, true), 711);
}

TEST(NewHorizonsElementalConvergencePowerTest, ExplicitCoefficientCastBonusesAndTwoFloorsRemainExact)
{
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(20, 12650, 20, 25, false), 439);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(20, 12650, 20, 25, true), 571);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(3, 14599, 0, 0, false), 271);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(3, 14667, 0, 0, false), 272);
}

TEST(NewHorizonsElementalConvergencePowerTest, NegativeAndZeroPowerPreserveTheFixedPool)
{
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(-1, 10000, 0, 0, false), 250);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(std::numeric_limits<int32_t>::min(), 10000, 0, 0, true), 325);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(0, 100000, 1000, 1000, false), 250);
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(100, 0, 1000, 1000, true), 325);
}

TEST(NewHorizonsElementalConvergencePowerTest, LargeValidHPBudgetHasNoInventedCountOrInt32Cap)
{
	EXPECT_EQ(newHorizonsMagic::elementalConvergenceHitPoints(std::numeric_limits<int32_t>::max(),
		100000, 1000, 1000, true), 16889958883980LL);
}

TEST(NewHorizonsElementalConvergencePowerTest, InvalidScalerDomainsRemainRejected)
{
	for(const int32_t coefficient : {-1, 100001})
		EXPECT_THROW(newHorizonsMagic::elementalConvergenceHitPoints(0, coefficient, 0, 0, false), std::invalid_argument);
	for(const int32_t bonus : {-1, 1001})
	{
		EXPECT_THROW(newHorizonsMagic::elementalConvergenceHitPoints(0, 10000, bonus, 0, false), std::invalid_argument);
		EXPECT_THROW(newHorizonsMagic::elementalConvergenceHitPoints(0, 10000, 0, bonus, false), std::invalid_argument);
	}
}
