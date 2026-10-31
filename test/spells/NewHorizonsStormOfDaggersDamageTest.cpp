/*
 * NewHorizonsStormOfDaggersDamageTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsStormOfDaggersDamage.h"
#include "../../lib/spells/NewHorizonsMagic.h"

#include <array>
#include <limits>

TEST(NewHorizonsStormOfDaggersDamageTest, CanonicalCountsKeepIndependentPoolAndShareRounding)
{
	const std::array<int64_t, 5> totals{195, 224, 254, 283, 312};
	const std::array<int64_t, 5> shares{195, 112, 85, 71, 62};
	ASSERT_EQ(totals.size(), newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS);
	for(int32_t count = 1; count <= newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS; ++count)
	{
		SCOPED_TRACE(count);
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, 10000, 0, 0, count, true), totals[count - 1]);
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, 10000, 0, 0, count, false), shares[count - 1]);
	}
	EXPECT_NE(shares[2] * 3, totals[2]);
}

TEST(NewHorizonsStormOfDaggersDamageTest, HalfValuesAndTargetSharesNeverRoundThePoolFirst)
{
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 1, 10, 10000, 0, 0, 1, false), 47);
	// Power3 creates49.5 before target scaling. Rounding that pool first gives
	//58 instead of57; dividing the rounded57 by2 half-up gives29 instead of28.
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 3, 10, 10000, 0, 0, 1, true), 50);
	const auto total = spells::stormOfDaggersRoundedDamage(45, 15, 3, 10, 10000, 0, 0, 2, true);
	const auto share = spells::stormOfDaggersRoundedDamage(45, 15, 3, 10, 10000, 0, 0, 2, false);
	EXPECT_EQ(total, 57);
	EXPECT_EQ(share, 28);
	EXPECT_NE(share, (total + 1) / 2);
}

TEST(NewHorizonsStormOfDaggersDamageTest, CapturedCoefficientCastBonusesAndDivisorRemainExplicit)
{
	const std::array<int32_t, 4> coefficients{10000, 11500, 13000, 14500};
	const std::array<int64_t, 4> expected{195, 218, 240, 263};
	for(size_t index = 0; index < coefficients.size(); ++index)
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10,
			coefficients[index], 0, 0, 1, false), expected[index]);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, 12650, 20, 25, 1, false), 330);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, 12650, 20, 25, 3, true), 429);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, 12650, 20, 25, 3, false), 143);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 100, 10000, 0, 0, 1, false), 60);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 1, 10000, 0, 0, 1, false), 1545);
}

TEST(NewHorizonsStormOfDaggersDamageTest, SavedFormulaAndFixedBaseAreNotReplacedOrScaled)
{
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(17, 4, 30, 10, 14599, 20, 25, 1, false), 43);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(17, 4, 0, 10, 100000, 1000, 1000, 1, false), 17);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(17, 4, 30, 10, 0, 1000, 1000, 1, false), 17);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(0, 0, 0, 10, 10000, 0, 0, 1, false), 0);
}

TEST(NewHorizonsStormOfDaggersDamageTest, InvalidDomainsKeepZeroResultBeforeArithmetic)
{
	const auto maximum = std::numeric_limits<int64_t>::max();
	for(const int32_t count : {0, -1, newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS + 1})
	{
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(maximum, maximum, 100, 10, 10000, 0, 0, count, false), 0);
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(maximum, maximum, 100, 10, 10000, 0, 0, count, true), 0);
	}
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(-1, 15, 100, 10, 10000, 0, 0, 1, false), 0);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, -1, 100, 10, 10000, 0, 0, 1, false), 0);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, -1, 10, 10000, 0, 0, 1, false), 0);
	EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 0, 10000, 0, 0, 1, false), 0);
	for(const int32_t coefficient : {-1, 100001})
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, coefficient, 0, 0, 1, false), 0);
	for(const int32_t bonus : {-1, 1001})
	{
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, 10000, bonus, 0, 1, false), 0);
		EXPECT_EQ(spells::stormOfDaggersRoundedDamage(45, 15, 100, 10, 10000, 0, bonus, 1, false), 0);
	}
}

TEST(NewHorizonsStormOfDaggersDamageTest, CheckedArithmeticOverflowStillThrows)
{
	const auto maximum = std::numeric_limits<int64_t>::max();
	EXPECT_THROW(spells::stormOfDaggersRoundedDamage(maximum, 15, 1, 10, 10000, 0, 0, 1, false), std::overflow_error);
	EXPECT_THROW(spells::stormOfDaggersRoundedDamage(45, maximum, 2, 10, 10000, 0, 0, 1, false), std::overflow_error);
	EXPECT_THROW(spells::stormOfDaggersRoundedDamage(0, 0, 0,
		std::numeric_limits<int32_t>::max(), 10000, 0, 0, 1, false), std::overflow_error);
}
