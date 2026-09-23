/*
 * MagicalDamageReductionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/spells/MagicalDamageReduction.h"

#include <limits>
#include <stdexcept>

using spells::MagicalDamageReductionResult;
using spells::calculateMagicalDamageReduction;

TEST(MagicalDamageReductionTest, IndependentSourcesMultiply)
{
	const auto result = calculateMagicalDamageReduction(100, {50, 50}, 0);
	EXPECT_EQ(result.damageWithoutPenetration, 25); // 50% remaining twice = 25% remaining / 75% MDR.
	EXPECT_EQ(result.damageWithPenetration, 25);
}

TEST(MagicalDamageReductionTest, PenetrationScalesTheAggregateMdrRelatively)
{
	const auto result = calculateMagicalDamageReduction(100, {50, 20}, 15);
	EXPECT_EQ(result.damageWithoutPenetration, 40); // Aggregate MDR is 60%.
	EXPECT_EQ(result.damageWithPenetration, 49); // 60% * (1 - 15%) = 51% MDR.
}

TEST(MagicalDamageReductionTest, AggregateMdrIsCappedBeforePenetration)
{
	const auto result = calculateMagicalDamageReduction(100, {95}, 15);
	EXPECT_EQ(result.damageWithoutPenetration, 5);
	EXPECT_EQ(result.damageWithPenetration, 19); // 95% * (1 - 15%) = 80.75% MDR.

	const auto sourcesExceedingTheCap = calculateMagicalDamageReduction(100, {90, 90}, 15);
	EXPECT_EQ(sourcesExceedingTheCap.damageWithoutPenetration, 5);
	EXPECT_EQ(sourcesExceedingTheCap.damageWithPenetration, 19);
}

TEST(MagicalDamageReductionTest, OneHundredPercentSourceCannotMakeDamageImmune)
{
	const auto result = calculateMagicalDamageReduction(100, {100}, 0);
	EXPECT_EQ(result.damageWithoutPenetration, 5);
	EXPECT_EQ(result.damageWithPenetration, 5);
}

TEST(MagicalDamageReductionTest, SourceOrderDoesNotAffectTheExactProduct)
{
	const auto first = calculateMagicalDamageReduction(987654321, {13, 50, 37}, 29);
	const auto reordered = calculateMagicalDamageReduction(987654321, {37, 13, 50}, 29);
	EXPECT_EQ(reordered, first);
}

TEST(MagicalDamageReductionTest, EmptySourcesAndZeroOrFullPenetrationAreIdentities)
{
	const auto noSources = calculateMagicalDamageReduction(123, {}, 0);
	EXPECT_EQ(noSources.damageWithoutPenetration, 123);
	EXPECT_EQ(noSources.damageWithPenetration, 123);

	const auto zeroPenetration = calculateMagicalDamageReduction(100, {80}, 0);
	EXPECT_EQ(zeroPenetration.damageWithoutPenetration, 20);
	EXPECT_EQ(zeroPenetration.damageWithPenetration, 20);

	const auto fullPenetration = calculateMagicalDamageReduction(100, {80}, 100);
	EXPECT_EQ(fullPenetration.damageWithoutPenetration, 20);
	EXPECT_EQ(fullPenetration.damageWithPenetration, 100);
}

TEST(MagicalDamageReductionTest, ZeroDamageStillValidatesAllArguments)
{
	const auto zero = calculateMagicalDamageReduction(0, {50, 100}, 15);
	EXPECT_EQ(zero.damageWithoutPenetration, 0);
	EXPECT_EQ(zero.damageWithPenetration, 0);

	EXPECT_THROW(calculateMagicalDamageReduction(0, {100, -1}, 0), std::invalid_argument);
	EXPECT_THROW(calculateMagicalDamageReduction(0, {}, 101), std::invalid_argument);
	EXPECT_THROW(calculateMagicalDamageReduction(100, {100, -1}, 0), std::invalid_argument);
}

TEST(MagicalDamageReductionTest, FloorsOnlyAfterTheExactCombinedFraction)
{
	// 3 * (1 - 50% * (1 - 50%)) = 2.25, floored once to 2.
	const auto result = calculateMagicalDamageReduction(3, {50}, 50);
	EXPECT_EQ(result.damageWithoutPenetration, 1); // floor(3 * 50%).
	EXPECT_EQ(result.damageWithPenetration, 2);

	// 7 * (67% * 67%) = 3.1423, floored once to 3 (not floored per source).
	const auto exactProduct = calculateMagicalDamageReduction(7, {33, 33}, 0);
	EXPECT_EQ(exactProduct.damageWithoutPenetration, 3);
	EXPECT_EQ(exactProduct.damageWithPenetration, 3);
}

TEST(MagicalDamageReductionTest, HandlesZeroMaximumAndManyFactorInputs)
{
	const auto zero = calculateMagicalDamageReduction(0, {100}, 100);
	EXPECT_EQ(zero, (MagicalDamageReductionResult{0, 0}));

	const int64_t maximum = std::numeric_limits<int64_t>::max();
	const auto noReduction = calculateMagicalDamageReduction(maximum, {}, 0);
	EXPECT_EQ(noReduction.damageWithoutPenetration, maximum);
	EXPECT_EQ(noReduction.damageWithPenetration, maximum);

	const auto capped = calculateMagicalDamageReduction(maximum, {100}, 0);
	EXPECT_EQ(capped.damageWithoutPenetration, maximum / 20);
	EXPECT_EQ(capped.damageWithPenetration, maximum / 20);
	const auto restored = calculateMagicalDamageReduction(maximum, {100}, 100);
	EXPECT_EQ(restored.damageWithoutPenetration, maximum / 20);
	EXPECT_EQ(restored.damageWithPenetration, maximum);

	const std::vector<int> manyFactors(512, 50);
	const auto many = calculateMagicalDamageReduction(maximum, manyFactors, 0);
	EXPECT_EQ(many.damageWithoutPenetration, maximum / 20);
}

TEST(MagicalDamageReductionTest, RejectsNegativeDamageAndOutOfRangePercentages)
{
	EXPECT_THROW(calculateMagicalDamageReduction(-1, {}, 0), std::invalid_argument);
	EXPECT_THROW(calculateMagicalDamageReduction(1, {}, -1), std::invalid_argument);
	EXPECT_THROW(calculateMagicalDamageReduction(1, {}, 101), std::invalid_argument);
	EXPECT_THROW(calculateMagicalDamageReduction(1, {-1}, 0), std::invalid_argument);
	EXPECT_THROW(calculateMagicalDamageReduction(1, {101}, 0), std::invalid_argument);
}
