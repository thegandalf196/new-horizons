/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativeReadView.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include <algorithm>
#include <stdexcept>
namespace forecast
{
namespace
{
void require(bool value)
{
	if(!value) throw std::invalid_argument("Incomplete accepted native read publication");
}
Counter observe(const battle::CAmmo & value)
{
	return {value.available(), value.total(), value.isLimited()};
}
void refreshNumbers(UnitDelta & next, const UnitDefinition & definition,
	const battle::CUnitState & source, bool changedProgram)
{
	// Query closure is independent of pair-fact support. HATE and other native
	// pair paths retain full native queries without manufacturing PreparedUnit.
	if(!changedProgram && !next.modifierNumbers) return;
	const int warfare = next.warfareStat;
	const auto count = next.health.getCount();
	const bool changedCount = next.modifierNumbers && next.modifierNumbers->prepared[0].unit.count != count;
	if(changedProgram || changedCount)
	{
		std::array<nativeDamage::PreparedUnit, 2> base;
		for(size_t mode = 0; mode < base.size(); ++mode)
		{
			auto value = nativeDamage::prepareUnit(source, mode != 0);
			if(!value)
			{
				next.numeric = {}; next.modifierNumbers.reset();
				return; // Exact native pair evaluation remains available.
			}
			base[mode] = std::move(*value);
		}
		auto numbers = next.currentModifiers->refresh(count, bonusDependencies::Change::Count,
			base, warfare, nullptr);
		require(numbers.has_value());
		for(size_t mode = 0; mode < base.size(); ++mode)
		{
			numbers->prepared[mode].leadershipEfficiency = definition.leadership.active
				? newHorizonsHeroes::squareRootLeadershipEfficiency(count, definition.leadership.capacity) : 1.0;
			next.numeric[mode] = std::make_shared<const nativeDamage::PreparedUnit>(numbers->prepared[mode]);
		}
		next.modifierNumbers = std::make_shared<const bonusDependencies::NumericFrame>(std::move(*numbers));
	}
	else
	{
		// Same-count HP changes update only already-owned native health facts.
		for(auto & prepared : next.numeric)
			if(prepared)
			{
				auto value = *prepared;
				value.unit.count = count;
				value.unit.firstHealth = next.health.getFirstHPleft();
				value.unit.maxHealth = next.health.valueContext().maximumHealth;
				value.unit.availableHealth = static_cast<double>(next.health.available());
				prepared = std::make_shared<const nativeDamage::PreparedUnit>(std::move(value));
			}
	}
}
}
void NativeAdapter::stageNativeReadValue(State & draft, UnitId id, const battle::CUnitState & source, int warfare)
{
	const auto * current = draft.unit(id);
	const auto * definition = draft.definition(id);
	require(current && definition && definition->readDescriptor && current->currentModifiers && current->queries);
	require(source.unitId() == id && !source.hasBattleFormState());
	auto next = *current;
	const bool changedProgram = !next.currentModifiers->ownsQueryFrame(*next.queries);
	const auto & descriptor = *definition->readDescriptor;
	bonusDependencies::QueryFrameBinding binding{source.unitId(), source.creatureId(), source.getCount(),
		source.unitOwner(), source.unitSide(), source.unitSlot(), source.ghost, source.summoned,
		source.natureSummoned, descriptor.creatures};
	auto queries = next.currentModifiers->refreshQueries(binding, bonusDependencies::Change::Count,
		changedProgram ? nullptr : next.queries);
	require(queries && next.currentModifiers->ownsQueryFrame(*queries));
	next.queries = std::move(queries);
	next.health.updateDetachedContext(source.health.valueContext());
	next.health = battle::CHealth::detached(source.health, source.health.valueContext());
	const int previousWarfare = next.warfareStat;
	require(warfare >= 0);
	next.warfareStat = warfare; // Exact environment result supplied by the accepted native owner.
	// Facts must not latch this receiver's gameplay/serialized round history.
	// Native observations run on a native counter copy; full capture below
	// retains the ACTUAL source cache, including an untouched round-reset zero.
	next.movement = source.getMovementRange(); next.initiative = source.getInitiative();
	const auto retal = source.counterAttacks.observeWithoutLatch();
	next.activation.retaliation = {retal.available, retal.total, retal.limited};
	next.activation.shots = observe(source.shots); next.activation.casts = observe(source.casts);
	next.activation.moved = source.movedThisRound; next.activation.waiting = source.waiting;
	next.activation.waited = source.waitedThisTurn; next.activation.defending = source.defending;
	next.activation.hadMorale = source.hadMorale;
	next.activation.waitBonusUsed = source.battlecraftWaitBonusUsed;
	next.activation.waitMasteryDoubled = source.battlecraftWaitMasteryDoubled;
	next.veteranPhysicalDamage = source.veteranPhysicalDamageSinceActivation;
	refreshNumbers(next, *definition, source, changedProgram || previousWarfare != next.warfareStat);
	auto values = std::make_shared<const battle::UnitStateValueSnapshot>(battle::UnitStateValueSnapshot::capture(source));
	require(values->identity() == descriptor.identity && values->status().count == next.queries->binding().count);
	next.nativeValues = std::move(values);
	draft.commit(id, std::move(next), Clean, false);
}
std::shared_ptr<const battle::UnitStateValueSnapshot> NativeAdapter::prepareReadValues(
	const HypotheticBattle & source, UnitId id)
{
	const auto * state = source.compactForecast ? &*source.compactForecast
		: source.compactReadSession ? &*source.compactReadSession : nullptr;
	require(state && state->readView(id));
	auto values = state->readView(id)->valuesOwner();
	const auto found = source.stackStates.find(id);
	if(found != source.stackStates.end())
	{
		require(found->second && found->second->owner == &source && found->second->unitId() == id);
		const auto cache = battle::UnitStateValueSnapshot::retaliationCache(*found->second);
		if(cache != values->retaliationCache())
			values = std::make_shared<const battle::UnitStateValueSnapshot>(values->withRetaliationCache(cache));
	}
	return values;
}
void NativeAdapter::stagePureReadValue(State & draft, HypotheticBattle & branch, UnitId id)
{
	const auto * current = draft.unit(id);
	require(current && draft.readView(id) && current->nativeValues && current->currentModifiers && current->queries);
	// Start from the ACTUAL owning reader, including native counter observations.
	// Never rebuild a state from a hand-maintained mutable field subset.
	const auto * registered = branch.battleGetUnitByID(id);
	require(registered && registered->unitId() == id);
	auto source = registered->acquireState();
	require(source && source->health.valueContext() == current->health.valueContext());
	source->health = current->health; // Canonical resolved ledger; do not damage/heal twice.
	if(source->health.isCapacityHealthTracking()) source->normalizeCapacityHealth();
	source->veteranPhysicalDamageSinceActivation = current->veteranPhysicalDamage;
	stageNativeReadValue(draft, id, *source, current->warfareStat);
}
void NativeAdapter::refreshReadViews(State & draft, HypotheticBattle & prospective,
	std::span<const UnitId> affected, bool contextChanged)
{
	if(affected.empty()) { require(!contextChanged); return; }
	const auto order = draft.orderedIds();
	std::set<UnitId> changed(affected.begin(), affected.end());
	require(changed.size() == affected.size());
	if(contextChanged) require(changed.size() == order.size());
	std::vector<NativeReadOccurrence> occurrences;
	occurrences.reserve(order.size());
	std::shared_ptr<const NativeReadEnvironment> environment;
	for(const auto id : order)
	{
		const auto * value = draft.unit(id);
		const auto * definition = draft.definition(id);
		require(value && definition && definition->readDescriptor && value->nativeValues && draft.readView(id));
		const auto previousEnvironment = draft.readView(id)->environmentOwner();
		require(previousEnvironment != nullptr);
		if(!environment) environment = previousEnvironment;
		else require(environment->revision == previousEnvironment->revision && environment->magic == previousEnvironment->magic);
		// Unchanged peers retain ACTUAL branch-local observation history.
		// A newly staged peer already captured its accepted native history.
		auto values = changed.contains(id) ? value->nativeValues : prepareReadValues(prospective, id);
		occurrences.push_back({definition->readDescriptor, std::move(values), value->currentModifiers, value->queries});
	}
	for(const auto id : changed) require(draft.unit(id) != nullptr);
	if(contextChanged) environment = captureReadEnvironment(prospective, std::move(environment));
	const auto views = NativeReadView::fromOwned(std::move(occurrences), std::move(environment), prospective.forecastWorkSink());
	require(views.size() == order.size());
	// No unaffected HP ledgers are copied. All preparation above precedes these
	// PRIVATE State changes; callers still publish only after every peer passes.
	for(size_t index = 0; index < order.size(); ++index)
		if(changed.contains(order[index]))
		{
			auto value = *draft.unit(order[index]);
			value.readView = views[index];
			draft.commit(order[index], std::move(value), Clean, false);
		}
}
}
