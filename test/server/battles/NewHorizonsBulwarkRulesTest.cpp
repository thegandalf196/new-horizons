/*
 * NewHorizonsBulwarkRulesTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../../lib/battle/NewHorizonsBulwark.h"

TEST(NewHorizonsBulwarkRules, RankFormulasUseExactBasisPoints)
{
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(1, 20, false), 700);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(2, 20, false), 1050);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(2, 1, false), 765);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(2, 37, false), 1305);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(3, 20, false), 1400);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(1, 0, false), 500);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(2, 0, false), 750);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(3, 0, false), 1000);
}

TEST(NewHorizonsBulwarkRules, MirebornAddsExactlyFivePercentagePoints)
{
	for(int rank = 1; rank <= 3; ++rank)
		EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(rank, 37, true),
			newHorizonsBulwark::reductionBasisPoints(rank, 37, false) + 500);
}

TEST(NewHorizonsBulwarkRules, InvalidRanksAndNegativeDefenseFailClosed)
{
	EXPECT_EQ(newHorizonsBulwark::rank(nullptr), 0);
	EXPECT_FALSE(newHorizonsBulwark::hasMireborn(nullptr));
	EXPECT_FALSE(newHorizonsBulwark::hasThickHide(nullptr));
	EXPECT_FALSE(newHorizonsBulwark::hasBogAmbush(nullptr));
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(0, 100, true), 0);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(4, 100, true), 0);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(1, -10, false), 500);
	EXPECT_EQ(newHorizonsBulwark::reductionBasisPoints(3, std::numeric_limits<int>::max(), true),
		std::numeric_limits<int>::max());
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(0), 0);
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(4), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectionPercent(0), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectionPercent(4), 0);
}

TEST(NewHorizonsBulwarkRules, PreemptiveAndReflectionScaleByRank)
{
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(1), 50);
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(2), 75);
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(3), 100);
	EXPECT_EQ(newHorizonsBulwark::reflectionPercent(1), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectionPercent(2), 25);
	EXPECT_EQ(newHorizonsBulwark::reflectionPercent(3), 50);
}

TEST(NewHorizonsBulwarkRules, BogAmbushAddsTwentyFivePercentagePointsAndCapsAtOneHundred)
{
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(1, true), 75);
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(2, true), 100);
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(3, true), 100);
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(0, true), 0);
	EXPECT_EQ(newHorizonsBulwark::preemptivePercent(4, true), 0);
}

TEST(NewHorizonsBulwarkRules, ThickHideReflectsHalfTheNormalPercentageOnlyAgainstRangedPhysicalHits)
{
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(1, false, false), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(2, false, false), 2500);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(3, false, false), 5000);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(2, true, false), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(2, true, true), 1250);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(3, true, true), 2500);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(3, false, true), 5000);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(0, true, true), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectionBasisPoints(4, true, true), 0);
}

TEST(NewHorizonsBulwarkRules, ReflectedDamageUsesActualHealthLossAndClampsItsPercentage)
{
	EXPECT_EQ(newHorizonsBulwark::reflectedDamage(1000, 2500), 250);
	EXPECT_EQ(newHorizonsBulwark::reflectedDamage(1000, 1250), 125);
	EXPECT_EQ(newHorizonsBulwark::reflectedDamage(3, 5000), 1);
	EXPECT_EQ(newHorizonsBulwark::reflectedDamage(1, 5000), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectedDamage(-1, 5000), 0);
	EXPECT_EQ(newHorizonsBulwark::reflectedDamage(200, 15000), 200);
	EXPECT_EQ(newHorizonsBulwark::reflectedDamage(std::numeric_limits<int64_t>::max(), 5000),
		std::numeric_limits<int64_t>::max() / 2);
}
