/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastEffectTransitions.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/battle/FocusMagicValueState.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include <stdexcept>
#include <algorithm>

namespace forecast
{
namespace
{
using Focus = newHorizonsFocusMagic::ValueState;
bool sameFocus(const Focus & left, const Focus & right)
{
	if(left.charges != right.charges || left.converted != right.converted
		|| left.records.size() != right.records.size()) return false;
	// Admitted Focus has at most one marker/shooter/damage component. Export
	// and effective-list order may differ, while each exact payload must match.
	for(const auto & record : left.records)
		if(!std::ranges::any_of(right.records, [&](const Bonus & candidate)
			{ return battle::exactBattleEffectEqual(record, candidate); })) return false;
	return true;
}
void require(bool accepted)
{
	if(!accepted) throw std::logic_error("Admitted Focus forecast lost its prepared receipt");
}
void requireIdentity(const HypotheticBattle & branch, const battle::CUnitState & target,
	const battle::CUnitState & accepted, const HypotheticBattle & acceptedWorld)
{
	require(acceptedWorld.getBattleID() == branch.getBattleID()
		&& acceptedWorld.battleGetRound() == branch.battleGetRound()
		&& acceptedWorld.getCapturedMagicRules() == branch.getCapturedMagicRules()
		&& accepted.unitId() == target.unitId() && accepted.creatureId() == target.creatureId()
		&& accepted.unitSide() == target.unitSide() && accepted.unitOwner() == target.unitOwner()
		&& accepted.unitBaseAmount() == target.unitBaseAmount()
		&& accepted.getHealth().valueContext() == target.getHealth().valueContext());
}
}
std::shared_ptr<const newHorizonsFocusMagic::ValueState> NativeAdapter::captureFocus(const battle::CUnitState & unit)
{
	auto value = newHorizonsFocusMagic::captureValueState(unit);
	if(!value) throw std::invalid_argument("Unsupported native Focus forecast record shape");
	return std::make_shared<const Focus>(std::move(*value));
}

bool NativeAdapter::afterFocusAttack(HypotheticBattle & branch, battle::CUnitState & target,
	bool ranged, bool counter, bool physical, bool accepted)
{
	if(!branch.compactForecast) return false;
	const auto id = target.unitId();
	const auto * current = branch.compactForecast->unit(id);
	require(current && current->focus && current->effects);
	if(!ranged || current->focus->charges == 0) return false;
	auto * isolated = dynamic_cast<StackWithBonuses *>(&target);
	require(isolated && branch.battleGetUnitByID(id) == isolated);
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node);
	const auto strike = newHorizonsFocusMagic::prepareStrike(*current->focus, accepted, ranged);
	require(strike.focusRanged == accepted
		&& (!accepted || strike.convertedRanged == current->focus->converted));
	auto after = accepted ? std::make_shared<const Focus>(strike.after) : current->focus;
	auto draft = *branch.compactForecast;
	const auto effects = prepareEffectApplication(draft, branch, id, strike.effects.remove, strike.effects.add);
	auto staged = prepareEffectDraft(*isolated);
	// Native CAmmo observes PRE-removal conversion and the real copied Cart env.
	// A zero-damage/missed accepted strike reaches this exact same boundary.
	staged->afterAttack(ranged, counter, physical);
	require(stageEffects(draft, branch, id, effects, *staged, std::move(after)));
	// All effect replay, numeric/counter queries and allocations already passed.
	const std::array<UnitId, 1> affected{id};
	refreshReadViews(draft, branch, affected, false);
	publishEffects(branch, std::move(draft), *isolated, *staged);
	return true;
}

void NativeAdapter::importAcceptedFocus(HypotheticBattle & branch, battle::CUnitState & target,
	const battle::CUnitState & accepted, const HypotheticBattle & acceptedWorld)
{
	if(!branch.compactForecast) return;
	const auto id = target.unitId();
	auto * isolated = dynamic_cast<StackWithBonuses *>(&target);
	require(isolated && branch.battleGetUnitByID(id) == isolated);
	requireIdentity(branch, target, accepted, acceptedWorld);
	const auto * current = branch.compactForecast->unit(id);
	require(current && current->focus && current->effects);
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node);
	auto after = captureFocus(accepted);
	if(sameFocus(*current->focus, *after)) return;
	auto draft = *branch.compactForecast;
	const auto effects = prepareEffectApplication(draft, branch, id, current->focus->records, after->records);
	auto staged = prepareEffectDraft(*isolated);
	// This is an already accepted native result. Do NOT call afterAttack or
	// prepareStrike: either would pay a second charge and/or ordinary shot.
	require(stageEffects(draft, branch, id, effects, *staged, std::move(after)));
	const std::array<UnitId, 1> affected{id};
	refreshReadViews(draft, branch, affected, false);
	publishEffects(branch, std::move(draft), *isolated, *staged);
}

void NativeAdapter::importFocusReceipt(State & draft, const battle::CUnitState & target,
	HypotheticBattle & branch)
{
	const auto id = target.unitId();
	require(branch.battleGetUnitByID(id) == &target);
	const auto * current = draft.unit(id);
	require(current && current->focus && current->effects);
	const auto native = captureFocus(target);
	std::vector<Bonus> managed;
	for(const auto & record : current->effects->values())
		if(record.kind == forecastEffects::Kind::FocusManaged) managed.push_back(*record.value);
	// Generic imported effects must arrive before this scalar-resource receipt.
	// A stale target cannot silently overwrite an accepted charge count.
	const auto effects = newHorizonsFocusMagic::valueState(std::move(managed));
	require(sameFocus(effects, *native));
	if(sameFocus(*current->focus, *native)) return;
	auto next = *current;
	next.focus = native;
	draft.commit(id, std::move(next), Effects | Queue, false);
}
}
