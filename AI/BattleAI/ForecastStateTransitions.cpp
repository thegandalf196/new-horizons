/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastStateTransitions.h"
#include "SearchBudget.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/OrdinaryUnitTransitions.h"
#include <algorithm>
#include <limits>
namespace forecast
{
namespace
{
Receipt receipt(const State & state, UnitId id, Event event)
{
	const auto & before = *state.unit(id);
	return {event, id, state.revision(), state.revision() + 1, 0,
		before.health.getCount(), before.health.getCount(), before.position, before.position};
}
void refreshNumeric(UnitDelta & value, const UnitDefinition & definition)
{
	// HP-only hits copy two flat health observations. Re-run captured native
	// scalar recipes/reducers only when the count dependency actually changed.
	if(value.modifierNumbers && value.modifierNumbers->prepared[0].unit.count != value.health.getCount())
	{
		if(!value.currentModifiers || !value.numeric[0] || !value.numeric[1])
			throw std::logic_error("Missing current compact modifier owner");
		auto base = std::array<nativeDamage::PreparedUnit, 2>{*value.numeric[0], *value.numeric[1]};
		for(auto & facts : base)
		{
			facts.unit.count = value.health.getCount();
			facts.unit.firstHealth = value.health.getFirstHPleft();
			facts.unit.maxHealth = value.health.valueContext().maximumHealth;
			facts.unit.availableHealth = static_cast<double>(value.health.available());
		}
		auto changed = value.currentModifiers->refresh(value.health.getCount(), bonusDependencies::Change::Count,
			base, value.warfareStat, value.modifierNumbers.get());
		if(!changed) throw std::logic_error("Admitted compact modifier count transition rejected");
		for(size_t mode = 0; mode < 2; ++mode)
		{
			if(definition.leadership.known) changed->prepared[mode].leadershipEfficiency = definition.leadership.active
				? newHorizonsHeroes::squareRootLeadershipEfficiency(value.health.getCount(), definition.leadership.capacity) : 1.0;
			value.numeric[mode] = std::make_shared<const nativeDamage::PreparedUnit>(changed->prepared[mode]);
		}
		value.modifierNumbers = std::make_shared<const bonusDependencies::NumericFrame>(std::move(*changed));
		return;
	}
	for(auto & prepared : value.numeric)
	{
		if(!prepared) continue;
		auto refreshed = std::make_shared<nativeDamage::PreparedUnit>(*prepared);
		if(definition.leadership.known) refreshed->leadershipEfficiency = definition.leadership.active
			? newHorizonsHeroes::squareRootLeadershipEfficiency(value.health.getCount(), definition.leadership.capacity) : 1.0;
		refreshed->unit.count = value.health.getCount();
		refreshed->unit.firstHealth = value.health.getFirstHPleft();
		refreshed->unit.maxHealth = value.health.valueContext().maximumHealth;
		refreshed->unit.availableHealth = static_cast<double>(value.health.available());
		prepared = std::move(refreshed);
	}
}
bool spend(Counter & counter)
{
	const auto remaining = battle::consumeCounterValue(counter.available, counter.limited, 1);
	if(!remaining) return false;
	counter.available = *remaining;
	return true;
}
}
Result Transitions::refuse(State & state, Refusal reason)
{
	state.counters.add(&Counters::refusedTransitions);
	return {reason, {}};
}
Refusal Transitions::preflight(State & state, UnitId id, Event event, battleSearch::SearchBudget * budget)
{
	if(!state.unit(id)) return Refusal::UnknownUnit;
	if(!state.supports(event)) return Refusal::Unsupported;
	if(state.revision() == std::numeric_limits<uint64_t>::max()) return Refusal::InvalidInput;
	if(budget && !budget->consume(battleSearch::WorkKind::Node)) return Refusal::Exhausted;
	return Refusal::None;
}
Result Transitions::applyDamage(State & state, UnitId id, int64_t amount,
	battle::DamageProvenance provenance, bool destroyRemains, battleSearch::SearchBudget * budget)
{
	if(amount < 0) return refuse(state, Refusal::InvalidInput);
	const auto refused = preflight(state, id, Event::Damage, budget);
	if(refused != Refusal::None) return refuse(state, refused);
	auto next = *state.unit(id); // All native ledger allocations precede commit.
	auto result = receipt(state, id, Event::Damage);
	const auto before = next.health.getCreatureHealthAvailable();
	next.health.damage(amount, destroyRemains, false, provenance, true);
	if(provenance == battle::DamageProvenance::PHYSICAL_CREATURE)
	{
		const auto lost = before - next.health.getCreatureHealthAvailable();
		next.veteranPhysicalDamage += std::min(lost,
			std::numeric_limits<int64_t>::max() - next.veteranPhysicalDamage);
	}
	if(next.health.available() == 0) next.veteranPhysicalDamage = 0;
	result.appliedHealth = amount;
	result.afterCount = next.health.getCount();
	refreshNumeric(next, *state.definition(id));
	state.commit(id, std::move(next), Health | Damage | Queue, result.afterCount == 0);
	state.counters.add(&Counters::detachedHealthUpdates);
	state.counters.add(&Counters::derivedRefreshes);
	return {Refusal::None, result};
}
Result Transitions::applyHealing(State & state, UnitId id, int64_t amount,
	EHealLevel level, EHealPower power, battleSearch::SearchBudget * budget)
{
	if((level == EHealLevel::HEAL && power == EHealPower::ONE_BATTLE)
		|| amount < 0 || (level != EHealLevel::HEAL && level != EHealLevel::RESURRECT && level != EHealLevel::OVERHEAL)
		|| (power != EHealPower::ONE_BATTLE && power != EHealPower::PERMANENT)) return refuse(state, Refusal::InvalidInput);
	const auto refused = preflight(state, id, Event::Healing, budget);
	if(refused != Refusal::None) return refuse(state, refused);
	auto next = *state.unit(id);
	auto result = receipt(state, id, Event::Healing);
	const auto healed = next.health.heal(amount, level, power);
	result.appliedHealth = healed.healedHealthPoints;
	result.healing = healed;
	result.afterCount = next.health.getCount();
	refreshNumeric(next, *state.definition(id));
	state.commit(id, std::move(next), Health | Damage | Queue, result.beforeCount == 0);
	state.counters.add(&Counters::detachedHealthUpdates);
	state.counters.add(&Counters::derivedRefreshes);
	return {Refusal::None, result};
}
Result Transitions::move(State & state, const MovePermit & permit, battleSearch::SearchBudget * budget)
{
	const auto refused = preflight(state, permit.unit, Event::Move, budget);
	if(refused != Refusal::None) return refuse(state, refused);
	if(budget && !budget->consume(battleSearch::WorkKind::Movement)) return refuse(state, Refusal::Exhausted);
	const auto & definition = *state.definition(permit.unit);
	if(!(permit.source == state.source()) || permit.layerIdentity != state.layer
		|| permit.revision != state.revision()
		|| state.unit(permit.unit)->position != permit.from) return refuse(state, Refusal::Stale);
	const auto & hexes = battle::Unit::getHexes(permit.to, definition.doubleWide, definition.side);
	if(hexes.empty() || std::ranges::any_of(hexes, [](BattleHex hex) { return !hex.isAvailable(); }))
		return refuse(state, Refusal::InvalidInput);
	for(const auto id : state.ids())
	{
		if(id == permit.unit || state.unit(id)->health.available() == 0) continue;
		const auto & other = *state.definition(id);
		const auto & occupied = battle::Unit::getHexes(state.unit(id)->position, other.doubleWide, other.side);
		for(const auto hex : hexes) if(occupied.contains(hex)) return refuse(state, Refusal::Occupied);
	}
	auto next = *state.unit(permit.unit);
	auto result = receipt(state, permit.unit, Event::Move);
	next.position = permit.to;
	result.afterPosition = permit.to;
	state.commit(permit.unit, std::move(next), Occupancy | Adjacency | Damage, true);
	return {Refusal::None, result};
}
Result Transitions::consumeActivation(State & state, UnitId id, battleSearch::SearchBudget * budget)
{
	const auto refused = preflight(state, id, Event::Activation, budget);
	if(refused != Refusal::None) return refuse(state, refused);
	auto next = *state.unit(id);
	if(next.activation.completed == std::numeric_limits<uint32_t>::max()) return refuse(state, Refusal::InvalidInput);
	auto result = receipt(state, id, Event::Activation);
	// Native exchange queues consume their slot without fabricating the live
	// movedThisRound flag. Keep this explicit ordinal ledger separate.
	++next.activation.completed;
	state.commit(id, std::move(next), Queue, false);
	return {Refusal::None, result};
}
Result Transitions::consumeRetaliation(State & state, UnitId id, battleSearch::SearchBudget * budget)
{
	const auto refused = preflight(state, id, Event::Retaliation, budget);
	if(refused != Refusal::None) return refuse(state, refused);
	auto next = *state.unit(id);
	if(!spend(next.activation.retaliation)) return refuse(state, Refusal::InvalidInput);
	auto result = receipt(state, id, Event::Retaliation);
	state.commit(id, std::move(next), Queue, false);
	return {Refusal::None, result};
}
Result Transitions::afterAttack(State & state, UnitId id, bool ranged, bool counter, bool physical,
	battleSearch::SearchBudget * budget)
{
	const auto refused = preflight(state, id, Event::Strike, budget);
	if(refused != Refusal::None) return refuse(state, refused);
	auto next = *state.unit(id);
	if((ranged && !spend(next.activation.shots)) || (counter && !spend(next.activation.retaliation)))
		return refuse(state, Refusal::InvalidInput);
	battle::finishOrdinaryAttackFlags(next.activation.waited, physical,
		next.activation.waitBonusUsed, next.activation.waitMasteryDoubled);
	auto result = receipt(state, id, Event::Strike);
	state.commit(id, std::move(next), Queue, false);
	return {Refusal::None, result};
}

bool Transitions::advanceRound(State & state, bool /* openingRound */, battleSearch::SearchBudget * budget, Refusal * failure)
{
	if(failure) *failure = Refusal::None;
	if(!state.supports(Event::Round) || state.round == std::numeric_limits<int32_t>::max())
	{
		if(failure) *failure = !state.supports(Event::Round) ? Refusal::Unsupported : Refusal::InvalidInput;
		state.counters.add(&Counters::refusedTransitions);
		return false;
	}
	// A failed unit/budget/allocation leaves the original branch unchanged.
	// Only this isolated transaction is discarded; no parent/sibling is touched.
	auto draft = state;
	for(const auto id : state.ids())
	{
		const auto refused = preflight(draft, id, Event::Round, budget);
		if(refused != Refusal::None)
		{
			if(failure) *failure = refused;
			state.counters.add(&Counters::refusedTransitions);
			return false;
		}
		auto next = *draft.unit(id);
		battle::resetOrdinaryRoundFlags(next.activation.defending, next.activation.waiting,
			next.activation.waited, next.activation.moved, next.activation.hadMorale,
			next.activation.waitBonusUsed, next.activation.waitMasteryDoubled);
		next.activation.retaliation.available = next.activation.retaliation.total;
		next.activation.completed = 0;
		draft.commit(id, std::move(next), Queue, false);
	}
	++draft.round;
	state = std::move(draft);
	return true;
}

}
