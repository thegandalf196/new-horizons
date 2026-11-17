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
	const auto unchangedNumbers = model->forecastState()->unit(id)->numeric;
	ASSERT_TRUE(unchangedNumbers[0]); ASSERT_TRUE(unchangedNumbers[1]);
	const auto unchangedEffects = model->forecastState()->unit(id)->effects;
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
	// This ordinary wait/resume/completion sequence changes native activation
	// state, but neither HP facts nor the exact local effect program.
	EXPECT_EQ(model->forecastState()->unit(id)->numeric, unchangedNumbers);
	EXPECT_EQ(model->forecastState()->unit(id)->effects, unchangedEffects);
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

TEST_F(ForecastNativeAdapterTest, UnchangedCompletionRetainsNativeOwnersOrdinalIsolationAndExactBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	for(const auto phase : {forecast::LifecyclePhase::CompleteNormal, forecast::LifecyclePhase::Finish})
	{
		auto model = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto reference = std::make_shared<HypotheticBattle>(environment.get(), parent);
		forecast::NativeAdapter::forkExchange(*parent, *model);
		const auto before = *model->forecastState();
		const auto ids = before.ids(); const auto actor = ids.front();
		const auto stable = model->getForUpdate(actor);
		const auto tree = model->getTreeVersion();
		battleSearch::WorkAmounts quota{};
		uint32_t known = 1 + 5 * static_cast<uint32_t>(ids.size());
		for(const auto id : ids) known += 3 * static_cast<uint32_t>(before.unit(id)->effects->values().size());
		quota[static_cast<size_t>(battleSearch::WorkKind::Node)] = known;
		auto budget = std::make_shared<battleSearch::SearchBudget>(quota);
		model->setSearchBudget(budget);
		const auto rng = projectedHitTest::serialized(*handler->randomizer);
		ASSERT_TRUE(forecast::NativeAdapter::lifecycle(*model, actor, BattleUnitTurnReason::TURN_QUEUE, phase));
		if(phase == forecast::LifecyclePhase::Finish) reference->finishProjectedActivation(actor);
		else reference->completeNormalActivation(actor);
		EXPECT_EQ(budget->spent()[static_cast<size_t>(battleSearch::WorkKind::Node)], ids.size() + 2);
		EXPECT_EQ(model->getTreeVersion(), tree + 1);
		EXPECT_EQ(model->forecastState()->revision(), before.revision() + 1);
		EXPECT_EQ(model->forecastState()->unit(actor)->activation.completed, before.unit(actor)->activation.completed + 1);
		EXPECT_EQ(parent->forecastState()->unit(actor)->activation.completed, before.unit(actor)->activation.completed);
		EXPECT_EQ(model->getForUpdate(actor), stable);
		for(const auto id : ids)
		{
			const auto * old = before.unit(id), * current = model->forecastState()->unit(id);
			EXPECT_EQ(current->nativeValues, old->nativeValues);
			EXPECT_EQ(model->forecastState()->readView(id), before.readView(id));
			EXPECT_EQ(current->effects, old->effects);
			EXPECT_EQ(current->queries, old->queries);
			EXPECT_EQ(current->numeric, old->numeric);
			EXPECT_EQ(model->getForUpdate(id)->save(), reference->getForUpdate(id)->save());
		}
		EXPECT_EQ(projectedHitTest::serialized(*handler->randomizer), rng);
	}
}

TEST_F(ForecastNativeAdapterTest, CompletionFastPathRetainsNativeFallbackForRealLifetimeAndFortuneChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto roster = parent->battleGetAllUnits(false);
	ASSERT_GE(roster.size(), 2u);
	const auto actor = roster.front()->unitId(), active = roster.back()->unitId();
	enum class Case { ForestShot, Tactics, Fortune, OtherActorMovement, Discipline, IronWill, CompletedCarry, IssuedNow };
	for(const auto phase : {forecast::LifecyclePhase::CompleteNormal, forecast::LifecyclePhase::Finish})
		for(const auto condition : {Case::ForestShot, Case::Tactics, Case::Fortune, Case::OtherActorMovement,
			Case::Discipline, Case::IronWill, Case::CompletedCarry, Case::IssuedNow})
		{
			SCOPED_TRACE(::testing::Message() << "condition " << static_cast<int>(condition)
				<< ", phase " << static_cast<int>(phase));
			auto model = std::make_shared<HypotheticBattle>(environment.get(), parent);
			model->nextRound();
			model->nextTurn(active, BattleUnitTurnReason::TURN_QUEUE);
			if(condition == Case::ForestShot)
			{
				model->getForUpdate(actor)->forestAmbush.waited = true;
				model->getForUpdate(actor)->forestAmbush.shotPending = true;
			}
			if(condition == Case::OtherActorMovement) model->getForUpdate(active)->setActivationMovementBonus(2);
			if(condition == Case::Tactics || condition == Case::Fortune)
			{
				auto fortune = model->getSylvanLuckState(BattleSide::DEFENDER);
				if(condition == Case::Tactics)
				{
					fortune.tactics.enabled = fortune.tactics.fleetfoot = true;
					fortune.tactics.activated.insert(active); fortune.tactics.firstActivation = active;
				}
				else
				{
					fortune.forestsFavor = true;
					fortune.positiveLuckUnits.insert(active); fortune.speedUnits.insert(active);
				}
				model->setSylvanLuckState(BattleSide::DEFENDER, fortune);
			}
			if(condition == Case::Discipline || condition == Case::IronWill
				|| condition == Case::CompletedCarry || condition == Case::IssuedNow)
			{
				HeroOrderState order; order.command = HeroCommand::BRACE; order.primaryTargetUnitId = actor;
				order.issuedRound = model->battleGetRound() - (condition == Case::IssuedNow ? 0 : 1);
				order.ironWillLifetime = condition == Case::IronWill;
				order.divineDisciplineRecipientUnitIds = {actor};
				if(condition == Case::CompletedCarry) order.divineDisciplineCompletedUnitIds = {actor};
				model->setHeroOrderStates(BattleSide::ATTACKER, {order});
			}
			ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*model));
			auto reference = std::make_shared<HypotheticBattle>(environment.get(), model);
			const auto before = *model->forecastState();
			const bool nativeChange = condition == Case::ForestShot || condition == Case::Tactics
				|| condition == Case::Discipline || condition == Case::IronWill
				|| (phase == forecast::LifecyclePhase::Finish
					&& (condition == Case::Fortune || condition == Case::OtherActorMovement));
			ASSERT_TRUE(forecast::NativeAdapter::lifecycle(*model, actor, BattleUnitTurnReason::TURN_QUEUE, phase));
			if(phase == forecast::LifecyclePhase::Finish) reference->finishProjectedActivation(actor);
			else reference->completeNormalActivation(actor);
			for(const auto id : before.ids())
			{
				EXPECT_EQ(model->getForUpdate(id)->save(), reference->getForUpdate(id)->save());
				if(nativeChange) EXPECT_NE(model->forecastState()->readView(id), before.readView(id));
				else EXPECT_EQ(model->forecastState()->readView(id), before.readView(id));
			}
			for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			{
				EXPECT_EQ(model->getSylvanLuckState(side), reference->getSylvanLuckState(side));
				EXPECT_EQ(model->getHeroOrderStates(side), reference->getHeroOrderStates(side));
			}
			EXPECT_EQ(model->getForUpdate(active)->getActivationMovementBonus(),
				reference->getForUpdate(active)->getActivationMovementBonus());
			EXPECT_EQ(model->forecastState()->unit(actor)->activation.completed, before.unit(actor)->activation.completed + 1);
		}
}

TEST_F(ForecastNativeAdapterTest, UnchangedCompletionKeepsFullReservationCutoffAndLazyMapOnRefusal)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(forecast::NativeAdapter::prepareExchange(*parent));
	for(const auto phase : {forecast::LifecyclePhase::CompleteNormal, forecast::LifecyclePhase::Finish})
	{
		auto model = std::make_shared<HypotheticBattle>(environment.get(), parent);
		forecast::NativeAdapter::forkExchange(*parent, *model);
		const auto before = *model->forecastState();
		const auto beforeMap = model->stackStates;
		battleSearch::WorkAmounts quota{};
		uint32_t known = 1 + 5 * static_cast<uint32_t>(before.ids().size());
		for(const auto id : before.ids()) known += 3 * static_cast<uint32_t>(before.unit(id)->effects->values().size());
		quota[static_cast<size_t>(battleSearch::WorkKind::Node)] = known - 1;
		auto budget = std::make_shared<battleSearch::SearchBudget>(quota); model->setSearchBudget(budget);
		EXPECT_THROW(forecast::NativeAdapter::lifecycle(*model, before.ids().front(), BattleUnitTurnReason::TURN_QUEUE, phase),
			battleSearch::BudgetExhausted);
		EXPECT_TRUE(model->forecastState()->sameSnapshot(before));
		EXPECT_EQ(model->stackStates, beforeMap);
		EXPECT_EQ(budget->spent()[static_cast<size_t>(battleSearch::WorkKind::Node)], 0u);
	}
}
