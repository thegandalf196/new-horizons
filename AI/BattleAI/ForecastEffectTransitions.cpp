/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastEffectTransitions.h"
#include "SearchBudget.h"
#include "../../lib/bonuses/BonusParameters.h"
#include <algorithm>
#include <limits>
#include <set>

namespace forecastEffects
{
bool Receipt::matches(const std::shared_ptr<const State> & current) const
{
	return before && current == before && current->revision() == expectedRevision;
}
Result Transitions::prepare(const std::shared_ptr<const State> & source, battleSearch::SearchBudget * budget)
{
	if(!source) return {Refusal::Unsupported, {}}; // null is NOT proof of absent effects
	if(source->revision() == std::numeric_limits<uint64_t>::max()) return {Refusal::Invalid, {}};
	if(budget && !budget->consume(battleSearch::WorkKind::Node)) return {Refusal::Exhausted, {}};
	auto next = State::copy(*source);
	++next->revisionValue;
	return {Refusal::None, Receipt{source, std::move(next), source->revision()}};
}
Dirty Transitions::dirty(const Record & record)
{
	if(record.kind == Kind::FocusManaged && record.value->type == BonusType::NONE) return Dirty::None;
	if(record.kind == Kind::Frozen || record.kind == Kind::Bind || record.kind == Kind::ForceLance)
		return Dirty::Control | Dirty::Queue;
	switch(record.value->type)
	{
	case BonusType::STACK_HEALTH: return Dirty::HealthCapacity | Dirty::UnitStats;
	case BonusType::STACKS_SPEED: case BonusType::STACKS_INITIATIVE: return Dirty::Queue | Dirty::UnitStats;
	case BonusType::ADDITIONAL_RETALIATION: return Dirty::Retaliation | Dirty::UnitStats;
	case BonusType::SPELL_DAMAGE_REDUCTION: case BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS:
	case BonusType::SPELL_IMMUNITY:
	case BonusType::LEVEL_SPELL_IMMUNITY: case BonusType::MAGIC_RESISTANCE: case BonusType::MIND_IMMUNITY:
		return Dirty::SpellDefense | Dirty::UnitStats;
	default: return Dirty::UnitStats;
	}
}
void Transitions::remove(State & next, Receipt & receipt, RecordId id)
{
	const auto found = std::ranges::find_if(next.records, [id](const Record & record) { return record.id == id; });
	if(found == next.records.end()) return;
	receipt.dirty = receipt.dirty | dirty(*found);
	receipt.changes.push_back({id, receipt.before->find(id)->value, {}});
	next.payloadOwners.erase(id);
	next.records.erase(found);
}
Result Transitions::prepareRound(const std::shared_ptr<const State> & source, bool openingRound,
	int32_t currentRound, battleSearch::SearchBudget * budget)
{
	if(!source) return {Refusal::Unsupported, {}};
	if(currentRound <= 0 || source->round() != currentRound - 1 || openingRound != (source->round() == 0))
		return {Refusal::Stale, {}};
	auto result = prepare(source, budget);
	if(!result) return result;
	auto & receipt = *result.receipt;
	auto next = std::const_pointer_cast<State>(receipt.after);
	next->admission.round = currentRound;
	next->forfeit.reset();
	if(openingRound) return result;
	for(const auto & before : source->values())
	{
		if(!before.roundClock || before.kind == Kind::FocusManaged) continue;
		const auto turns = battle::timedEffects::nextTurns(before.value->turnsRemain);
		if(turns <= 0) { remove(*next, receipt, before.id); continue; }
		auto changed = std::make_shared<Bonus>(*before.value);
		changed->turnsRemain = static_cast<int16_t>(turns);
		const auto found = std::ranges::find_if(next->records, [&](const Record & record) { return record.id == before.id; });
		found->value = changed;
		receipt.changes.push_back({before.id, before.value, std::move(changed)});
		// Remaining duration changes do not alter native numeric stats yet.
	}
	return result;
}
Result Transitions::prepareActivation(const std::shared_ptr<const State> & source, Activation input,
	battleSearch::SearchBudget * budget)
{
	if(!source) return {Refusal::Unsupported, {}};
	auto result = prepare(source, budget);
	if(!result) return result;
	const bool frozen = source->frozen();
	const bool forfeits = battle::timedEffects::frozenForfeits(input.reason, input.alive, frozen);
	if(input.normalSlotForfeit != forfeits || (input.ghost && input.alive)) return {Refusal::Invalid, {}};
	if(source->pendingForfeit() && input.serial <= source->pendingForfeit()->activation)
		return {Refusal::Stale, {}};
	auto & receipt = *result.receipt;
	auto next = std::const_pointer_cast<State>(receipt.after);
	if(frozen && !forfeits)
	{
		receipt.blockedExtraActivation = true;
		return result; // extras/continuations cannot spend the normal slot or thaw
	}
	if(battle::timedEffects::activationContinuation(input.reason)) return result;
	for(const auto & record : source->values())
		if(record.kind != Kind::FocusManaged
			&& battle::timedEffects::activationExpires(*record.value, input.reason, input.beginsActivation, false))
			remove(*next, receipt, record.id);
	if(forfeits)
	{
		next->forfeit = Forfeit{input.serial, input.reason, false};
		receipt.consumedNormalSlot = true;
		receipt.dirty = receipt.dirty | Dirty::Queue | Dirty::Control;
		if(!input.deferFrozenThaw)
		{
			// Same preplanned completion; no second publication or budget reset.
			next->forfeit->completed = true;
			for(const auto & record : source->values())
				if(battle::timedEffects::activationEndExpires(*record.value, false)
					|| (record.kind == Kind::Frozen && battle::timedEffects::frozenThaws(input.reason,
						true, true, input.alive, input.ghost))) remove(*next, receipt, record.id);
		}
	}
	return result;
}
Result Transitions::prepareActivationEnd(const std::shared_ptr<const State> & source, uint64_t serial,
	bool alive, bool ghost, battleSearch::SearchBudget * budget)
{
	if(!source) return {Refusal::Unsupported, {}};
	const auto pending = source->pendingForfeit();
	if(pending && (pending->activation != serial || pending->completed)) return {Refusal::Stale, {}};
	if(alive && ghost) return {Refusal::Invalid, {}};
	auto result = prepare(source, budget);
	if(!result) return result;
	auto & receipt = *result.receipt;
	auto next = std::const_pointer_cast<State>(receipt.after);
	if(next->forfeit) next->forfeit->completed = true;
	for(const auto & record : source->values())
		if(record.kind != Kind::FocusManaged
			&& (battle::timedEffects::activationEndExpires(*record.value, false)
				|| (record.kind == Kind::Frozen && pending && battle::timedEffects::frozenThaws(pending->reason,
					true, true, alive, ghost)))) remove(*next, receipt, record.id);
	return result;
}
Result Transitions::prepareApplication(const std::shared_ptr<const State> & source,
	std::span<const RecordId> removed, std::span<const RawRecord> incoming,
	std::optional<int32_t> frozenApplicationRound, battleSearch::SearchBudget * budget)
{
	if(!source) return {Refusal::Unsupported, {}};
	if(incoming.size() > std::numeric_limits<RecordId>::max() - source->nextRecordId)
		return {Refusal::Invalid, {}};
	auto result = prepare(source, budget);
	if(!result) return result;
	std::set<RecordId> selected;
	for(const auto id : removed)
		if(!source->find(id) || !selected.insert(id).second) return {Refusal::Stale, {}};
	auto admission = source->admission;
	if(frozenApplicationRound)
	{
		if(*frozenApplicationRound < admission.frozenApplicationRound || *frozenApplicationRound > admission.round)
			return {Refusal::Invalid, {}};
		admission.frozenApplicationRound = *frozenApplicationRound;
	}
	std::shared_ptr<const State> additions;
	try { additions = State::capture(incoming, admission); }
	catch(const std::invalid_argument &) { return {Refusal::Unsupported, {}}; }
	catch(const std::runtime_error &) { return {Refusal::Unsupported, {}}; }
	auto & receipt = *result.receipt;
	auto next = std::const_pointer_cast<State>(receipt.after);
	for(const auto id : removed) remove(*next, receipt, id);
	for(const auto & added : additions->values())
	{
		if(std::ranges::any_of(next->records, [&](const Record & record) { return record.origin == added.origin; }))
			return {Refusal::Unsupported, {}};
		auto record = added;
		record.id = static_cast<RecordId>(next->nextRecordId++);
		receipt.dirty = receipt.dirty | dirty(record);
		receipt.changes.push_back({record.id, {}, record.value});
		next->payloadOwners.emplace(record.id, additions->payloadOwners.at(added.id));
		next->records.push_back(std::move(record));
	}
	next->admission.frozenApplicationRound = admission.frozenApplicationRound;
	return result;
}
Result Transitions::prepareFrozenShatter(const std::shared_ptr<const State> & source,
	bool nativeQualified, battleSearch::SearchBudget * budget)
{
	return prepareRemoval(source, Removal::FrozenShatter, nativeQualified, {}, budget);
}
Result Transitions::prepareStrike(const std::shared_ptr<const State> & source,
	battle::timedEffects::StrikeClock clock, battleSearch::SearchBudget * budget)
{
	auto result = prepare(source, budget);
	if(!result) return result;
	auto & receipt = *result.receipt;
	auto next = std::const_pointer_cast<State>(receipt.after);
	for(const auto & record : source->values())
		if(record.kind != Kind::FocusManaged && battle::timedEffects::strikeExpires(*record.value, clock))
			remove(*next, receipt, record.id);
	return result;
}
Result Transitions::prepareRemoval(const std::shared_ptr<const State> & source, Removal reason,
	bool actuallyMoved, std::span<const BindObservation> observations, battleSearch::SearchBudget * budget)
{
	if(!source) return {Refusal::Unsupported, {}};
	auto result = prepare(source, budget);
	if(!result) return result;
	std::vector<battle::timedEffects::BindAnchor> anchors;
	if(reason == Removal::LegacyBindTrigger)
	{
		std::set<RecordId> used;
		for(const auto & record : source->values())
		{
			if(record.kind != Kind::Bind && record.kind != Kind::ForceLance) continue;
			if(!record.value->parameters) { anchors.push_back({false, false}); continue; }
			const auto found = std::ranges::find_if(observations, [&](const BindObservation & value) { return value.id == record.id; });
			if(found == observations.end() || !used.insert(record.id).second) return {Refusal::Unsupported, {}};
			anchors.push_back({true, found->sourceAliveAdjacent});
		}
		if(used.size() != observations.size()) return {Refusal::Invalid, {}};
	}
	auto & receipt = *result.receipt;
	auto next = std::const_pointer_cast<State>(receipt.after);
	const bool unbind = reason == Removal::LegacyBindTrigger && battle::timedEffects::legacyBindReleases(anchors);
	for(const auto & record : source->values())
	{
		const bool removeRecord = (reason == Removal::TargetMoved
				&& battle::timedEffects::targetMoveBreaks(*record.value, actuallyMoved, source->admission.entangle))
			|| (reason == Removal::FrozenShatter && actuallyMoved && record.kind == Kind::Frozen)
			|| (reason == Removal::DeathOrResurrection && record.transientOnDeath)
			|| (unbind && (record.kind == Kind::Bind || record.kind == Kind::ForceLance));
		if(removeRecord) remove(*next, receipt, record.id);
	}
	return result;
}
}
