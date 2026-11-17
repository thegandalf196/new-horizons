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
bool NativeAdapter::moveUnit(HypotheticBattle & branch, UnitId actor, BattleHex destination)
{
	if(!branch.compactForecast) return false;
	if(!branch.compactForecast->supports(Event::Move) || !branch.compactForecast->unit(actor))
		throw std::logic_error("Movement has no admitted current unit recipe");
	auto draft = *branch.compactForecast;
	const auto expected = draft.layer;
	const auto ids = draft.ids();
	uint64_t known = 1 + 2 * static_cast<uint64_t>(ids.size());
	for(const auto id : ids) known += 3 * draft.unit(id)->effects->values().size() + 1;
	if(known > std::numeric_limits<uint32_t>::max()) throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
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
	HypotheticBattle future(branch.env, std::move(borrowed), {}, work);
	future.compactForecast.reset();
	std::vector<std::pair<std::shared_ptr<StackWithBonuses>, std::shared_ptr<StackWithBonuses>>> peers;
	for(const auto id : ids)
	{
		auto target = preparation.unit(id);
		auto staged = std::shared_ptr<StackWithBonuses>(prepareEffectDraft(*target, &future));
		future.stackStates[id] = staged;
		peers.emplace_back(std::move(target), std::move(staged));
	}
	// SAME trusted native packet operation: Entangle/source-local expiry and
	// HoldTheLine/Protect membership remain native; no path solver is invented.
	future.moveUnit(actor, destination);
	for(const auto & [target, staged] : peers)
	{
		const auto id = target->unitId();
		const auto after = captureEffects(*staged, future.battleGetRound());
		std::vector<Bonus> removed, added;
		for(const auto & row : draft.unit(id)->effects->values()) removed.push_back(*row.value);
		for(const auto & row : after->values()) added.push_back(*row.value);
		const auto before = draft.unit(id)->effects;
		forecastEffects::Receipt receipt{before, before, before->revision()};
		const bool identical = removed.size() == added.size() && std::equal(removed.begin(), removed.end(), added.begin(),
			[](const Bonus & left, const Bonus & right) { return battle::exactBattleEffectEqual(left, right); });
		if(!identical) receipt = prepareEffectApplication(draft, future, id, removed, added);
		if(!stageEffects(draft, branch, id, receipt, *staged, nullptr, true))
			throw std::logic_error("Native movement effect receipt rejected");
		auto next = *draft.unit(id);
		const bool moved = next.position != staged->getPosition();
		next.position = staged->getPosition();
		draft.commit(id, std::move(next), moved ? Occupancy | Adjacency | Damage : Clean, moved);
	}
	refreshOrderDerived(future, draft);
	if(!branch.compactForecast || branch.compactForecast->layer != expected)
		throw std::logic_error("Stale native movement state");
	for(const auto & [target, staged] : peers)
		if(!target->preparedEffectSourceMatches(*staged)) throw std::logic_error("Stale native movement recipient");
	preparation.accept(ids);
	for(const auto & [target, staged] : peers)
	{
		target->publishPreparedEffects(*staged);
		battle::exchangePreparedUnitStateValues(*target, *staged);
	}
	branch.heroOrderStates.swap(future.heroOrderStates);
	++branch.bonusTreeVersion;
	branch.compactForecast = std::move(draft);
	return true;
}
}
