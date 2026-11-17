/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapterFixture.h"

TEST_F(ForecastNativeAdapterTest, NativeActivationWaitCompletionAndFinalRoundUseSameAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *model);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), visible);
	const auto id = model->forecastState()->ids().front();
	const auto compare = [&]
	{
		for(const auto current : model->forecastState()->ids())
		{
			EXPECT_EQ(model->getForUpdate(current)->save(), reference->getForUpdate(current)->save());
			const auto * value = model->forecastState()->unit(current);
			const auto * state = dynamic_cast<const battle::CUnitState *>(model->battleGetUnitByID(current));
			ASSERT_NE(state, nullptr);
			EXPECT_EQ(value->activation.waited, state->waitedThisTurn);
			EXPECT_EQ(value->activation.waiting, state->waiting);
		}
		EXPECT_EQ(model->battleGetRound(), reference->battleGetRound());
	};
	const auto authority = projectedHitTest::serialized(*handler->randomizer);
	const auto ambient = projectedHitTest::serialized(CRandomGenerator::getDefault());
	model->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	reference->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	compare();
	model->makeWait(model->battleGetUnitByID(id));
	reference->makeWait(reference->battleGetUnitByID(id));
	compare();
	model->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	reference->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE);
	model->completeNormalActivation(id);
	reference->completeNormalActivation(id);
	compare();
	for(int iteration = 0; iteration < 2; ++iteration)
	{
		model->nextRound(); reference->nextRound(); compare();
		EXPECT_EQ(model->forecastState()->currentRound(), model->battleGetRound());
	}
	EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), authority);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), ambient);
}

TEST_F(ForecastNativeAdapterTest, LifecycleBudgetRefusalKeepsExactLazyMapAndModel)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	forecast::NativeAdapter::forkExchange(*parent, *branch);
	const auto beforeMap = branch->stackStates;
	const auto before = *branch->forecastState();
	battleSearch::WorkAmounts none{};
	branch->setSearchBudget(std::make_shared<battleSearch::SearchBudget>(none));
	const auto id = before.ids().front();
	EXPECT_THROW(branch->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE), battleSearch::BudgetExhausted);
	EXPECT_EQ(branch->stackStates, beforeMap);
	ASSERT_NE(branch->forecastState(), nullptr);
	EXPECT_TRUE(branch->forecastState()->sameSnapshot(before));
	EXPECT_EQ(branch->battleGetRound(), parent->battleGetRound());
}
