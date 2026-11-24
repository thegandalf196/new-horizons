/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativePreparation.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include <limits>
#include <stdexcept>
namespace forecast
{
namespace
{
void require(bool accepted)
{
	if(!accepted) throw std::logic_error("Admitted forecast lifecycle rejected before publication");
}
// Match the native helpers on private values rather than maintaining another
// list of perk/lifetime predicates. Any real mutation retains the full replay.
bool unchangedCompletion(const HypotheticBattle & branch, const StackWithBonuses & actor, LifecyclePhase phase)
{
	if(actor.forestAmbush.shotPending || actor.sylvanAmbush.strikePending) return false;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto before = branch.getSylvanLuckState(side);
		auto after = before;
		if(phase == LifecyclePhase::Finish) after.endActivation();
		else after.tactics.endActivation();
		if(after != before) return false;
		auto orders = branch.localHeroOrderStates(side);
		if(newHorizonsDivineMandate::completeDisciplineOrders(orders, branch.getRound(), actor.unitId()))
			return false;
	}
	return true;
}
std::vector<Bonus> records(const forecastEffects::State & effects)
{
	std::vector<Bonus> result;
	result.reserve(effects.values().size());
	for(const auto & record : effects.values()) result.push_back(*record.value);
	return result;
}
// Identical captured effects can retain their existing owned Bonus values.
bool sameEffectValues(const forecastEffects::State & before, const forecastEffects::State & after)
{
	const auto & previous = before.values();
	const auto & current = after.values();
	return previous.size() == current.size()
		&& std::equal(previous.begin(), previous.end(), current.begin(), [](const auto & left, const auto & right)
		{ return battle::exactBattleEffectEqual(*left.value, *right.value); });
}
std::shared_ptr<battleSearch::SearchBudget> reserveLifecycleWork(const State & draft,
	const std::shared_ptr<battleSearch::SearchBudget> & budget)
{
	const auto ids = draft.ids();
	uint64_t known = 1 + 4 * static_cast<uint64_t>(ids.size());
	for(const auto id : ids)
	{
		require(draft.unit(id)->effects != nullptr);
		known += 3 * draft.unit(id)->effects->values().size() + 1;
	}
	if(known > std::numeric_limits<uint32_t>::max())
		throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	if(!budget) return {};
	battleSearch::WorkAmounts quota{};
	quota[static_cast<size_t>(battleSearch::WorkKind::Node)] = static_cast<uint32_t>(known);
	auto reserved = budget->reserve(quota);
	if(!reserved) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	return std::make_shared<battleSearch::SearchBudget>(std::move(*reserved));
}
}
bool NativeAdapter::consumeUnchangedCompletion(HypotheticBattle & branch, NativePreparation & preparation,
	State & draft, UnitId actor, LifecyclePhase phase, battleSearch::SearchBudget * work)
{
	if(phase != LifecyclePhase::Finish && phase != LifecyclePhase::CompleteNormal) return false;
	const auto target = preparation.unit(actor);
	require(target != nullptr);
	if(phase == LifecyclePhase::Finish && branch.activeUnitId >= 0)
	{
		const auto active = static_cast<uint32_t>(branch.activeUnitId);
		if((branch.stackStates.contains(active) || branch.subject->battleGetUnitByID(active))
			&& preparation.unit(active)->getActivationMovementBonus() != 0) return false;
	}
	if(!unchangedCompletion(branch, *target, phase)) return false;
	// Keep the full replay's reserve cutoff and constructor charge even
	// when the native owners can be retained unchanged.
	if(work) work->require(battleSearch::WorkKind::Node, static_cast<uint32_t>(draft.ids().size()) + 1);
	const auto completed = Transitions::consumeActivation(draft, actor, work);
	if(completed.refusal == Refusal::Exhausted) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	require(static_cast<bool>(completed));
	return true;
}
bool NativeAdapter::lifecycle(HypotheticBattle & branch, UnitId actor,
	BattleUnitTurnReason reason, LifecyclePhase phase, HypotheticLifecycleTiming * timing)
{
	if(!branch.compactForecast) return false;
	require(branch.compactForecast->unit(actor) != nullptr);
	if(phase == LifecyclePhase::Finish && branch.currentForfeitCompleted(actor)) return true;
	if(phase == LifecyclePhase::CompleteForfeit
		&& (!branch.wasCurrentActivationForfeited(actor) || branch.currentForfeitCompleted(actor))) return true;
	auto draft = *branch.compactForecast;
	const auto expected = branch.compactForecast->layer;
	const auto ids = draft.ids();
	auto work = reserveLifecycleWork(draft, branch.searchBudget());
	NativePreparation preparation(branch);
	if(consumeUnchangedCompletion(branch, preparation, draft, actor, phase, work.get()))
	{
		require(branch.compactForecast && branch.compactForecast->layer == expected);
		preparation.accept(ids);
		++branch.bonusTreeVersion;
		branch.compactForecast = std::move(draft);
		return true;
	}
	auto borrowed = std::shared_ptr<CBattleInfoCallback>(&branch, [](CBattleInfoCallback *) {});
	auto future = std::make_shared<HypotheticBattle>(branch.env, std::move(borrowed),
		std::shared_ptr<const battle::OwnedBattleFrameBatch>{}, work);
	// Native lifecycle remains the single authority. No value-model callback
	// is allowed to recursively publish while replaying into prospective state.
	future->compactForecast.reset();
	std::vector<std::pair<std::shared_ptr<StackWithBonuses>, std::shared_ptr<StackWithBonuses>>> peers;
	peers.reserve(ids.size());
	for(const auto id : ids)
	{
		auto target = preparation.unit(id);
		require(target != nullptr);
		auto staged = std::shared_ptr<StackWithBonuses>(prepareEffectDraft(*target, future.get()));
		future->stackStates[id] = staged;
		peers.emplace_back(std::move(target), std::move(staged));
	}
	switch(phase)
	{
	case LifecyclePhase::Begin: future->nextTurnImpl(actor, reason, false, timing); break;
	case LifecyclePhase::BeginDeferred: future->nextTurnImpl(actor, reason, true, timing); break;
	case LifecyclePhase::CompleteForfeit: future->completeForfeitedActivation(actor); break;
	case LifecyclePhase::CompleteNormal: future->completeNormalActivation(actor); break;
	case LifecyclePhase::Finish: future->finishProjectedActivation(actor); break;
	case LifecyclePhase::Wait: future->makeWait(future->battleGetUnitByID(actor)); break;
	}
	for(const auto & [target, staged] : peers)
	{
		const auto id = target->unitId();
		const auto before = draft.unit(id)->effects;
		const auto after = captureEffects(*staged, branch.battleGetRound());
		forecastEffects::Receipt receipt{before, before, before->revision()};
		if(!sameEffectValues(*before, *after))
			receipt = prepareEffectApplication(draft, *future, id, records(*before), records(*after),
				staged->frozenLastAppliedRound());
		require(stageEffects(draft, branch, id, receipt, *staged, nullptr, true));
	}
	if(phase == LifecyclePhase::CompleteNormal || phase == LifecyclePhase::CompleteForfeit || phase == LifecyclePhase::Finish)
	{
		const auto completed = Transitions::consumeActivation(draft, actor, work.get());
		if(completed.refusal == Refusal::Exhausted) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
		require(static_cast<bool>(completed));
	}
	refreshReadViews(draft, *future, draft.orderedIds(), true);
	require(branch.compactForecast && branch.compactForecast->layer == expected);
	for(const auto & [target, staged] : peers)
		require(branch.battleGetUnitByID(target->unitId()) == target.get() && target->preparedEffectSourceMatches(*staged));
	// All native work, allocations, effect imports and numeric preparation
	// completed before this storage-only publication boundary.
	preparation.accept(ids);
	for(const auto & [target, staged] : peers)
	{
		target->publishPreparedEffects(*staged);
		battle::exchangePreparedUnitStateValues(*target, *staged);
	}
#define NH_SWAP_LIFECYCLE(field) static_assert(std::is_nothrow_swappable_v<decltype(branch.field)>); std::swap(branch.field, future->field)
	NH_SWAP_LIFECYCLE(activeUnitId);
	NH_SWAP_LIFECYCLE(seizeInitiative);
	NH_SWAP_LIFECYCLE(forfeitedActivations);
	exchangeSylvanLuckSides(branch.fortuneStates, future->fortuneStates);
	NH_SWAP_LIFECYCLE(rapidResponseStates);
	NH_SWAP_LIFECYCLE(relentlessAssaultStates);
	NH_SWAP_LIFECYCLE(sideStates);
	NH_SWAP_LIFECYCLE(heroOrderStates);
	NH_SWAP_LIFECYCLE(focusFireStates);
#undef NH_SWAP_LIFECYCLE
	++branch.bonusTreeVersion;
	branch.compactForecast = std::move(draft);
	return true;
}
}
