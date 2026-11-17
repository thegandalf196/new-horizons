/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../../AI/BattleAI/HeroDecisionPressure.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include <limits>

TEST(HeroRefinementBudgetTest, StableTopThreeUsesOrdinalEvaluationOrder)
{
	using heroDecisionPressure::Ranked;
	const std::vector<Ranked> values{{0, 4}, {1, 7}, {2, 7}, {3, -1}, {4, 5}, {5, 7}};
	EXPECT_EQ(heroDecisionPressure::boundedRefinements(values, 3), (std::vector<size_t>{1, 2, 5}));
	EXPECT_EQ(heroDecisionPressure::boundedRefinements(values, 2), (std::vector<size_t>{1, 2}));
}
TEST(HeroRefinementBudgetTest, UnknownEstimateRetainsFullAndNegativeValueIsNotIllegal)
{
	using heroDecisionPressure::Ranked;
	const std::vector<Ranked> unknown{{0, 4}, {1, std::numeric_limits<float>::quiet_NaN()}, {2, 7}};
	EXPECT_EQ(heroDecisionPressure::boundedRefinements(unknown, 1), (std::vector<size_t>{0, 1, 2}));
	const std::vector<Ranked> negative{{0, -4}, {1, -1}, {2, -7}, {3, -8}};
	EXPECT_EQ(heroDecisionPressure::boundedRefinements(negative, 2), (std::vector<size_t>{0, 1}));
	EXPECT_EQ(heroDecisionPressure::boundedRefinements(negative, 0), (std::vector<size_t>{0, 1, 2, 3}));
}
TEST(HeroRefinementBudgetTest, FastBudgetDoesNotChangeExistingCreatureDepthOrFullProfile)
{
	using namespace battleSearch;
	EXPECT_EQ(heroRefinementLimit(), 0u);
	{
		const Scope quick(Profile::BeamOneTwoActors);
		EXPECT_EQ(heroRefinementLimit(), 3u);
		EXPECT_EQ(forecastFutureActors(), 2u);
		{
			const Scope full(Profile::Full);
			EXPECT_EQ(heroRefinementLimit(), 0u);
			EXPECT_FALSE(forecastBeamOne());
		}
		EXPECT_EQ(heroRefinementLimit(), 3u);
	}
	EXPECT_EQ(heroRefinementLimit(), 0u);
}
