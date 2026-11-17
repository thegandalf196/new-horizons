/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativePreparation.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/battle/UnitStateValueExchange.h"
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
std::vector<Bonus> records(const forecastEffects::State & effects)
{
	std::vector<Bonus> result;
	result.reserve(effects.values().size());
	for(const auto & record : effects.values()) result.push_back(*record.value);
	return result;
}
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
	uint64_t known = 1 + 4 * static_cast<uint64_t>(ids.size());
	for(const auto id : ids)
	{
		require(draft.unit(id)->effects != nullptr);
		known += 3 * draft.unit(id)->effects->values().size() + 1;
	}
	if(known > std::numeric_limits<uint32_t>::max())
		throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	std::shared_ptr<battleSearch::SearchBudget> work;
	if(branch.searchBudget())
	{
		battleSearch::WorkAmounts quota{};
		quota[static_cast<size_t>(battleSearch::WorkKind::Node)] = static_cast<uint32_t>(known);
		auto reserved = branch.searchBudget()->reserve(quota);
		if(!reserved) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
		work = std::make_shared<battleSearch::SearchBudget>(std::move(*reserved));
	}
	NativePreparation preparation(branch);
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
		auto removed = records(*before), added = records(*after);
		forecastEffects::Receipt receipt{before, before, before->revision()};
		const bool identical = removed.size() == added.size() && std::equal(removed.begin(), removed.end(), added.begin(),
			[](const Bonus & left, const Bonus & right) { return battle::exactBattleEffectEqual(left, right); });
		if(!identical)
			receipt = prepareEffectApplication(draft, *future, id, removed, added,
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
	NH_SWAP_LIFECYCLE(fortuneStates);
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
