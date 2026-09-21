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
