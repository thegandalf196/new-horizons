/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../../AI/BattleAI/RecoverableEvaluation.h"
#include <stdexcept>

TEST(RecoverableEvaluationTest, ReturnsTheEvaluationWhenItSucceeds)
{
	int recovered = 0;
	EXPECT_EQ(battleAI::recoverEvaluation("test", nullptr, [] { return 7; },
		[&] { ++recovered; return std::optional<int>(1); }), 7);
	EXPECT_EQ(recovered, 0);
}

TEST(RecoverableEvaluationTest, ForecastFailureUsesTheLegalFallback)
{
	EXPECT_EQ(battleAI::recoverEvaluation("test", nullptr, []() -> int { throw std::invalid_argument("forecast"); },
		[] { return std::optional<int>(3); }), 3);
}

TEST(RecoverableEvaluationTest, BudgetExhaustionAndStartedSubmissionsPropagate)
{
	EXPECT_THROW(battleAI::recoverEvaluation("test", nullptr,
		[]() -> int { throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node); },
		[] { return std::optional<int>(3); }), battleSearch::BudgetExhausted);
	EXPECT_THROW(battleAI::recoverEvaluation("test", nullptr, []() -> int { throw std::runtime_error("submission"); },
		[] { return std::optional<int>(); }), std::runtime_error);
}
