/*
 * ArcaneOverflowValueTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../AI/BattleAI/ArcaneOverflowValue.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"

#include <limits>

namespace
{
using arcaneOverflowAI::Outcome;
using arcaneOverflowAI::LocalUtility;
newHorizonsMetamagic::OverflowPreview preview(int primaryResistance, int secondaryResistance)
{
	newHorizonsMetamagic::OverflowPreview result;
	result.primaryResistancePercent = primaryResistance;
	result.secondaryResistancePercent = secondaryResistance;
	return result;
}
battleSearch::WorkAmounts limits(uint32_t nodes)
{
	battleSearch::WorkAmounts result{};
	result[static_cast<size_t>(battleSearch::WorkKind::Node)] = nodes;
	return result;
}
TEST(ArcaneOverflowValueTest, NoResistanceVisitsOnlyBothLandAndDoesNotReweightConditionalValue)
{
	battleSearch::SearchBudget budget(limits(1));
	int visits = 0;
	const auto value = arcaneOverflowAI::expectedLocalValue(preview(0, 0), budget,
		[&](Outcome outcome) -> std::optional<LocalUtility>
		{
			++visits;
			EXPECT_EQ(outcome, Outcome::BothLand);
			return 80;
		});
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(value->immediate, 80);
	EXPECT_EQ(visits, 1);
}
TEST(ArcaneOverflowValueTest, CompletelyResistedOriginalStillValuesItsOneAcceptedCompletion)
{
	battleSearch::SearchBudget budget(limits(1));
	int visits = 0;
	const auto value = arcaneOverflowAI::expectedLocalValue(preview(100, 0), budget,
		[&](Outcome outcome) -> std::optional<LocalUtility>
		{
			++visits;
			EXPECT_EQ(outcome, Outcome::PrimaryResisted);
			return 7;
		});
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(value->immediate, 7);
	EXPECT_EQ(visits, 1);
}
TEST(ArcaneOverflowValueTest, ImmuneOrCompletelyResistantSecondaryRetainsPrimaryOnly)
{
	battleSearch::SearchBudget budget(limits(1));
	const auto value = arcaneOverflowAI::expectedLocalValue(preview(0, 100), budget,
		[](Outcome outcome) -> std::optional<LocalUtility>
		{
			EXPECT_EQ(outcome, Outcome::PrimaryOnly);
			return 30;
		});
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(value->immediate, 30);
}
TEST(ArcaneOverflowValueTest, IntermediateResistanceWeightsOriginalAndConditionalSpillOnce)
{
	battleSearch::SearchBudget budget(limits(3));
	std::vector<Outcome> visits;
	const auto value = arcaneOverflowAI::expectedLocalValue(preview(25, 40), budget,
		[&](Outcome outcome) -> std::optional<LocalUtility>
		{
			visits.push_back(outcome);
			switch(outcome)
			{
			case Outcome::PrimaryResisted: return 8;
			case Outcome::PrimaryOnly: return 48;
			case Outcome::BothLand: return 88;
			}
			return std::nullopt;
		});
	ASSERT_TRUE(value);
	// .25*8 + .75*.40*48 + .75*.60*88. Neither 40-HP gain is preweighted.
	EXPECT_FLOAT_EQ(value->immediate, 56);
	EXPECT_EQ(visits, (std::vector{Outcome::PrimaryResisted, Outcome::PrimaryOnly, Outcome::BothLand}));
}
TEST(ArcaneOverflowValueTest, ConstantAcceptedCompletionEntersExactlyOnceAcrossOutcomes)
{
	battleSearch::SearchBudget budget(limits(3));
	const auto value = arcaneOverflowAI::expectedLocalValue(preview(50, 50), budget,
		[](Outcome) -> std::optional<LocalUtility> { return LocalUtility{0, 9}; });
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(value->immediate, 0);
	EXPECT_FLOAT_EQ(value->completion, 9);
}
TEST(ArcaneOverflowValueTest, WholeFiniteVisitChargePrecedesFirstEffect)
{
	battleSearch::SearchBudget budget(limits(2));
	int effects = 0;
	EXPECT_THROW(arcaneOverflowAI::expectedLocalValue(preview(50, 50), budget,
		[&](Outcome) -> std::optional<LocalUtility> { ++effects; return 1; }), battleSearch::BudgetExhausted);
	EXPECT_EQ(effects, 0);
}
TEST(ArcaneOverflowValueTest, ZeroBudgetCannotPublishAnEvaluatedUtility)
{
	battleSearch::SearchBudget budget(limits(0));
	int effects = 0;
	EXPECT_THROW(arcaneOverflowAI::expectedLocalValue(preview(0, 0), budget,
		[&](Outcome) -> std::optional<LocalUtility> { ++effects; return 100; }), battleSearch::BudgetExhausted);
	EXPECT_EQ(effects, 0);
}
TEST(ArcaneOverflowValueTest, UnknownPositiveOutcomeIsNotZeroOrAnIllegalCandidate)
{
	battleSearch::SearchBudget budget(limits(3));
	const auto value = arcaneOverflowAI::expectedLocalValue(preview(50, 50), budget,
		[](Outcome outcome) -> std::optional<LocalUtility>
		{
			return outcome == Outcome::PrimaryOnly ? std::nullopt : std::optional<LocalUtility>(100);
		});
	EXPECT_FALSE(value);
}
TEST(ArcaneOverflowValueTest, ZeroProbabilityUnknownOutcomeIsNeverInvoked)
{
	battleSearch::SearchBudget budget(limits(1));
	const auto value = arcaneOverflowAI::expectedLocalValue(preview(0, 0), budget,
		[](Outcome outcome) -> std::optional<LocalUtility>
		{
			if(outcome != Outcome::BothLand) return std::nullopt;
			return 10;
		});
	ASSERT_TRUE(value);
	EXPECT_FLOAT_EQ(value->immediate, 10);
}
TEST(ArcaneOverflowValueTest, NativeProjectionBudgetFailurePropagatesWithoutFallback)
{
	battleSearch::SearchBudget budget(limits(3));
	EXPECT_THROW(arcaneOverflowAI::expectedLocalValue(preview(0, 0), budget,
		[](Outcome) -> std::optional<LocalUtility> { throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Movement); }),
		battleSearch::BudgetExhausted);
}
TEST(ArcaneOverflowValueTest, MalformedResistanceAndNonFiniteKernelRemainUnknown)
{
	battleSearch::SearchBudget budget(limits(3));
	int visits = 0;
	const auto evaluator = [&](Outcome) -> std::optional<LocalUtility> { ++visits; return 1; };
	EXPECT_FALSE(arcaneOverflowAI::expectedLocalValue(preview(-1, 0), budget, evaluator));
	EXPECT_FALSE(arcaneOverflowAI::expectedLocalValue(preview(0, 101), budget, evaluator));
	EXPECT_EQ(visits, 0);
	EXPECT_FALSE(arcaneOverflowAI::expectedLocalValue(preview(0, 0), budget,
		[](Outcome) -> std::optional<LocalUtility> { return std::numeric_limits<float>::quiet_NaN(); }));
}
TEST(ArcaneOverflowValueTest, SeedDispatcherRequestsAtMostThreeMaterializationsAndNeverScoresZeroMassSeed)
{
	struct Case { int primary, secondary, materializations, seedScores; };
	const std::array cases{Case{0, 0, 1, 1}, Case{0, 100, 2, 0}, Case{100, 0, 2, 0},
		Case{50, 0, 2, 1}, Case{50, 50, 3, 1}, Case{50, 100, 3, 0}};
	for(const auto & test : cases)
	{
		SCOPED_TRACE(test.primary);
		SCOPED_TRACE(test.secondary);
		battleSearch::SearchBudget budget(limits(3));
		int materializations = 1; // The authentic post-kill seed already exists.
		int seedScores = 0;
		const auto seedValue = [&]() -> std::optional<LocalUtility>
		{
			++seedScores;
			return LocalUtility{1, 4};
		};
		const auto otherValue = [&](Outcome outcome) -> std::optional<LocalUtility>
		{
			EXPECT_NE(outcome, Outcome::BothLand);
			++materializations;
			return LocalUtility{1, 4};
		};
		const auto value = arcaneOverflowAI::expectedSeededLocalValue(
			preview(test.primary, test.secondary), budget, seedValue, otherValue);
		ASSERT_TRUE(value);
		EXPECT_FLOAT_EQ(value->immediate, 1);
		EXPECT_FLOAT_EQ(value->completion, 4);
		EXPECT_EQ(materializations, test.materializations);
		EXPECT_LE(materializations, 3);
		EXPECT_EQ(seedScores, test.seedScores);
	}
}
TEST(ArcaneOverflowValueTest, SeedDiscoveryDoesNotMakeAnIncompletePositiveOutcomePublishable)
{
	battleSearch::SearchBudget budget(limits(3));
	int seedScores = 0;
	const auto seedValue = [&]() -> std::optional<LocalUtility> { ++seedScores; return 100; };
	const auto otherValue = [](Outcome) -> std::optional<LocalUtility> { return std::nullopt; };
	EXPECT_FALSE(arcaneOverflowAI::expectedSeededLocalValue(preview(50, 50), budget, seedValue, otherValue));
	EXPECT_EQ(seedScores, 0); // Stable primary-resisted visit fails before BothLand utility.
}

TEST(ArcaneOverflowValueTest, CascadeRetainsFullTwoRecipientUtilityAndOneAcceptedCompletion)
{
	auto plan = preview(50, 0);
	plan.additionalRecipients.push_back(preview(50, 0));
	battleSearch::SearchBudget budget(limits(2));
	int visits = 0;
	const auto result = arcaneOverflowAI::expectedLocalValue(plan, budget,
		[&](Outcome outcome) -> std::optional<LocalUtility>
		{
			++visits;
			EXPECT_NE(outcome, Outcome::PrimaryOnly);
			return LocalUtility{outcome == Outcome::BothLand ? 100 + 120 + 120 : 0, 9};
		});
	ASSERT_TRUE(result);
	EXPECT_FLOAT_EQ(result->immediate, 170);
	EXPECT_FLOAT_EQ(result->completion, 9);
	EXPECT_EQ(visits, 2);
}

TEST(ArcaneOverflowValueTest, IndependentCascadeResistanceIsExplicitlyUnknownBeforeUtility)
{
	for(const auto resistance : {std::pair{0, 50}, std::pair{50, 0}, std::pair{100, 0}})
	{
		auto plan = preview(0, resistance.first);
		plan.additionalRecipients.push_back(preview(0, resistance.second));
		battleSearch::SearchBudget budget(limits(3));
		int visits = 0;
		EXPECT_FALSE(arcaneOverflowAI::expectedLocalValue(plan, budget,
			[&](Outcome) -> std::optional<LocalUtility> { ++visits; return 100; }));
		EXPECT_EQ(visits, 0);
	}
}

}
