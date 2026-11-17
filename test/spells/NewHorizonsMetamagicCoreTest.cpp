/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMetamagicCore.h"
#include <limits>
using namespace newHorizonsMetamagic;
TEST(MetamagicCore, ExpertRetryAgainstHalfResistanceHasExactlySixtyFivePercentSuccess)
{
	int failures = 0;
	bool bounded = true;
	// Enumerate the complete original-resistance/chance/retry probability space.
	for(int first = 0; first < 100; ++first)
		for(int chance = 0; chance < 100; ++chance)
			for(int retry = 0; retry < 100; ++retry)
			{
				int draws = 0;
				failures += retryResistance(first < 50, 60, 50, [&]() { return draws++ == 0 ? chance : retry; });
				bounded &= draws <= 2;
			}
	EXPECT_EQ(failures, 350000);
	EXPECT_TRUE(bounded);
	EXPECT_DOUBLE_EQ(resistanceFailureProbability(50, 60), .35);
}
TEST(MetamagicCore, RetryConsumesNoDrawOnSuccessAndAtMostOneNormalRecheckAfterChance)
{
	int draws = 0;
	const auto sample = [&]() { ++draws; return 99; };
	EXPECT_FALSE(retryResistance(false, 60, 50, sample));
	EXPECT_TRUE(retryResistance(true, 0, 50, sample));
	EXPECT_EQ(draws, 0);
	EXPECT_TRUE(retryResistance(true, 60, 50, sample));
	EXPECT_EQ(draws, 1);
	draws = 0;
	EXPECT_TRUE(retryResistance(true, 100, 100, [&]() { ++draws; return 0; }));
	EXPECT_EQ(draws, 2); // Even retry cannot bypass certain normal resistance.
}
TEST(MetamagicCore, FriendlyReductionHasOneFloorAndNoWideMultiplicationOverflow)
{
	EXPECT_EQ(retainedFriendlyDamage(1000, 70), 700);
	EXPECT_EQ(retainedFriendlyDamage(1000, 55), 550);
	EXPECT_EQ(retainedFriendlyDamage(1000, 40), 400);
	EXPECT_EQ(retainedFriendlyDamage(19, 40), 7);
	EXPECT_EQ(retainedFriendlyDamage(1000, 0), 0);
	const auto largest = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(retainedFriendlyDamage(largest, 100), largest);
	EXPECT_EQ(retainedFriendlyDamage(largest, 40), largest / 100 * 40 + largest % 100 * 40 / 100);
}
TEST(MetamagicCore, FractionalSteadfastResistanceSurvivesTheSingleNormalRetryCheck)
{
	int draws = 0;
	EXPECT_FALSE(retryResistanceBasisPoints(true, 60, 2250, [&](int maximum)
	{
		++draws;
		if(draws == 1) { EXPECT_EQ(maximum, 99); return 0; }
		EXPECT_EQ(maximum, 9999); return 2250;
	}));
	EXPECT_EQ(draws, 2);
	EXPECT_TRUE(retryResistanceBasisPoints(true, 60, 2250, [&](int maximum)
		{ return maximum == 99 ? 0 : 2249; }));
	EXPECT_NEAR(resistanceFailureProbabilityBasisPoints(2250, 60, .225), .120375, 1e-12);
}
