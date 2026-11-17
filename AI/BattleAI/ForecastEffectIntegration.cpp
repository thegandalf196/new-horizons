/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "ForecastNativePreparation.h"
#include "ForecastEffectLimits.h"
#include "ForecastNativeReadView.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/RoundBonusDuration.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "../../lib/battle/UnitTransitionSidecars.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyOverlay.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace forecast
{
namespace
{

void require(bool value)
{
	if(!value) throw std::logic_error("Admitted effect transaction is stale or unsupported");
}
forecastEffects::Receipt accepted(forecastEffects::Result result)
{
	if(result.refusal == forecastEffects::Refusal::Exhausted)
		throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
	if(!result) throw std::invalid_argument("Unsupported owned effect transition");
	return std::move(*result.receipt);
}
void applyNativeEffects(StackWithBonuses & staged, const forecastEffects::Receipt & receipt)
{
	auto spells = staged.localSpellEffects();
	std::vector<Bonus> removedOther, addedOther;
	bool changedSpells = false;
	for(const auto & change : receipt.changes)
	{
		if(change.before && change.before->source == BonusSource::SPELL_EFFECT)
		{
			const auto found = std::ranges::find_if(spells, [&](const Bonus & value)
			{ return battle::exactBattleEffectEqual(value, *change.before); });
			require(found != spells.end());
			spells.erase(found); changedSpells = true;
		}
		else if(change.before) removedOther.push_back(*change.before);
		if(change.after && change.after->source == BonusSource::SPELL_EFFECT)
		{
			spells.push_back(*change.after); changedSpells = true;
		}
		else if(change.after) addedOther.push_back(*change.after);
	}
	if(changedSpells) staged.replaceLocalSpellEffectsExact(spells);
	if(!removedOther.empty()) staged.removeUnitBonus(removedOther);
	if(!addedOther.empty()) staged.addUnitBonus(addedOther);
}
bool sameFamily(const Bonus & left, const Bonus & right)
{
	return left.source == right.source && left.sid == right.sid && left.type == right.type
		&& left.subtype == right.subtype && left.valType == right.valType && left.stacking == right.stacking;
}
}
std::unique_ptr<StackWithBonuses> NativeAdapter::prepareEffectDraft(const StackWithBonuses & target, const HypotheticBattle * prospective)
{
	return target.prepareEffectDraft(prospective);
}
void NativeAdapter::expireActivationEffects(HypotheticBattle & branch, StackWithBonuses & actor)
{
	const CSelector selector([](const Bonus * bonus)
	{
		return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
	});
	if(!expireEffectSelector(branch, actor.unitId(), selector)) actor.removeUnitBonus(selector);
}
bool NativeAdapter::stageEffects(State & draft, HypotheticBattle & branch, UnitId id,
	const forecastEffects::Receipt & receipt, StackWithBonuses & staged,
	std::shared_ptr<const newHorizonsFocusMagic::ValueState> focusAfter, bool receiptAlreadyApplied, bool roundReset)
{
	const auto * current = draft.unit(id);
	const auto * target = dynamic_cast<const StackWithBonuses *>(branch.battleGetUnitByID(id));
	if(!current || !target || !receipt.matches(current->effects)
		|| !target->preparedEffectSourceMatches(staged)) return false;
	if(!receiptAlreadyApplied) applyNativeEffects(staged, receipt); // ALL work stays private
	auto next = *current;
	next.effects = receipt.after;
	bool focusChanged = false;
	for(const auto & change : receipt.changes)
	{
		const auto * before = receipt.before->find(change.id);
		const auto * after = receipt.after->find(change.id);
		focusChanged |= (before && before->kind == forecastEffects::Kind::FocusManaged)
			|| (after && after->kind == forecastEffects::Kind::FocusManaged);
	}
	if(focusAfter) next.focus = std::move(focusAfter);
	else if(focusChanged) next.focus = captureFocus(staged);
	const bool healthChanged = next.health.available() != staged.health.available()
		|| next.health.getCount() != staged.health.getCount()
		|| next.health.valueContext() != staged.health.valueContext();
	// Raw replacements own a new program even when their numbers are unchanged.
	if(!receipt.changes.empty())
	{
		auto replacement = target->capturePreparedModifierFacts(staged);
		if(!replacement || !draft.definition(id)->readDescriptor) return false;
		const auto & descriptor = *draft.definition(id)->readDescriptor;
		bonusDependencies::QueryFrameBinding binding{staged.unitId(), staged.creatureId(), staged.getCount(),
			staged.unitOwner(), staged.unitSide(), staged.unitSlot(), staged.ghost, staged.summoned,
			staged.natureSummoned, descriptor.creatures};
		auto queries = replacement->refreshQueries(binding, bonusDependencies::Change::Count);
		if(!queries || !staged.stageOwnedModifierFacts(replacement, queries)) return false;
		next.currentModifiers = std::move(replacement);
	}
	if(roundReset) next.activation.completed = 0;
	uint32_t dirty = Effects;
	if(healthChanged) dirty |= Health | Damage;
	if(receipt.dirty != forecastEffects::Dirty::None) dirty |= Damage;
	if(forecastEffects::contains(receipt.dirty, forecastEffects::Dirty::Queue)) dirty |= Queue;
	draft.commit(id, std::move(next), dirty, false);
	stageNativeReadValue(draft, id, staged, staged.preparedModifierWarfare());
	return true;
}
void NativeAdapter::publishEffects(HypotheticBattle & branch, State && draft,
	StackWithBonuses & target, StackWithBonuses & staged) noexcept
{
	assert(target.preparedEffectSourceMatches(staged));
	static_assert(std::is_nothrow_move_assignable_v<decltype(branch.compactForecast)>);
	target.publishPreparedEffects(staged);
	battle::exchangePreparedUnitStateValues(target, staged);
	branch.compactForecast = std::move(draft);
}

forecastEffects::Receipt NativeAdapter::prepareEffectApplication(const State & source, HypotheticBattle & branch,
	UnitId id, const std::vector<Bonus> & removed, const std::vector<Bonus> & added,
	std::optional<int32_t> frozenStamp)
{
	const auto * current = source.unit(id);
	if(!current || !current->effects || removed.size() + added.size() > MAXIMUM_EFFECT_RECORDS)
		throw std::invalid_argument("Effect application has no admitted local exporter");
	// One work debit covers this finite accepted receipt, before enumeration.
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(removed.size() + added.size() + current->effects->values().size() + 1));
	for(const auto * rows : {&removed, &added})
		for(const auto & row : *rows)
			if(!bonusDependencies::supportsRecipientRecord(row))
				throw std::invalid_argument("Unsupported executable/opaque recipient effect row");
	std::vector<forecastEffects::RecordId> ids;
	std::vector<forecastEffects::RawRecord> incoming;
	uint64_t nextOccurrence = 0;
	for(const auto & record : current->effects->values())
	{
		if(record.origin.owner != static_cast<uint64_t>(id)
			|| record.origin.occurrence == std::numeric_limits<uint64_t>::max())
			throw std::invalid_argument("Opaque or exhausted native clock owner");
		nextOccurrence = std::max(nextOccurrence, record.origin.occurrence + 1);
	}
	for(const auto & value : removed)
	{
		const auto found = std::ranges::find_if(current->effects->values(), [&](const auto & record)
		{
			return !vstd::contains(ids, record.id) && battle::exactBattleEffectEqual(*record.value, value);
		});
		if(found == current->effects->values().end()) throw std::invalid_argument("Stale accepted effect removal");
		ids.push_back(found->id);
	}
	std::set<uint64_t> reused;
	for(const auto & value : added)
	{
		std::optional<forecastEffects::Origin> origin;
		for(const auto old : ids)
		{
			const auto * record = current->effects->find(old);
			if(sameFamily(*record->value, value) && reused.insert(record->origin.occurrence).second)
			{ origin = record->origin; break; }
		}
		if(!origin)
		{
			if(nextOccurrence == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Effect clock exhausted");
			origin = forecastEffects::Origin{static_cast<uint64_t>(id), nextOccurrence++};
		}
		incoming.push_back({std::make_shared<const Bonus>(value), *origin, Bonus::NTurns(&value),
			battle::isTransientSpellEffect(&value)});
	}
	return accepted(forecastEffects::Transitions::prepareApplication(current->effects, ids, incoming, frozenStamp));
}
bool NativeAdapter::effectApplication(HypotheticBattle & branch, UnitId id,
	const std::vector<Bonus> & removed, const std::vector<Bonus> & added, std::optional<int32_t> frozenStamp)
{
	if(!branch.compactForecast) return false;
	auto draft = *branch.compactForecast;
	const auto receipt = prepareEffectApplication(draft, branch, id, removed, added, frozenStamp);
	NativePreparation preparation(branch);
	auto target = preparation.unit(id);
	require(target != nullptr);
	auto staged = prepareEffectDraft(*target);
	require(stageEffects(draft, branch, id, receipt, *staged));
	refreshReadViews(draft, branch, std::span(&id, 1), false);
	preparation.accept(std::span(&id, 1));
	publishEffects(branch, std::move(draft), *target, *staged);
	return true;
}
bool NativeAdapter::effectUpdate(HypotheticBattle & branch, UnitId id, const std::vector<Bonus> & incoming)
{
	if(!branch.compactForecast) return false;
	auto draft = *branch.compactForecast;
	std::optional<int32_t> stamp;
	for(const auto & row : incoming)
		if(const auto round = newHorizonsFrozen::markerApplicationRound(row)) stamp = round;
	// Admit executable family/payloads BEFORE invoking even private native
	// hooks (unsupported Serpent/opaque hooks can touch their owning battle).
	(void)prepareEffectApplication(draft, branch, id, {}, incoming, stamp);
	NativePreparation preparation(branch);
	auto target = preparation.unit(id);
	auto staged = prepareEffectDraft(*target);
	staged->updateUnitBonus(incoming); // authoritative native update/refresh semantics
	const auto final = captureEffects(*staged, draft.currentRound());
	std::vector<Bonus> removed, added;
	std::vector<bool> matched(final->values().size(), false);
	for(const auto & old : draft.unit(id)->effects->values())
	{
		bool found = false;
		for(size_t index = 0; index < matched.size(); ++index)
			if(!matched[index] && battle::exactBattleEffectEqual(*old.value, *final->values()[index].value))
			{ matched[index] = true; found = true; break; }
		if(!found) removed.push_back(*old.value);
	}
	for(size_t index = 0; index < matched.size(); ++index)
		if(!matched[index]) added.push_back(*final->values()[index].value);
	const auto receipt = prepareEffectApplication(draft, branch, id, removed, added,
		staged->frozenLastAppliedRound() >= 0 ? std::optional<int32_t>(staged->frozenLastAppliedRound()) : std::nullopt);
	require(stageEffects(draft, branch, id, receipt, *staged, nullptr, true));
	refreshReadViews(draft, branch, std::span(&id, 1), false);
	preparation.accept(std::span(&id, 1));
	publishEffects(branch, std::move(draft), *target, *staged);
	return true;
}
bool NativeAdapter::effectRemoval(HypotheticBattle & branch, UnitId id,
	forecastEffects::Removal reason, bool acceptedEvent)
{
	if(!branch.compactForecast) return false;
	auto draft = *branch.compactForecast;
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node);
	require(draft.unit(id) && draft.unit(id)->effects);
	std::vector<forecastEffects::BindObservation> observations;
	if(reason == forecastEffects::Removal::LegacyBindTrigger)
	{
		const auto * current = branch.battleGetUnitByID(id);
		require(current != nullptr);
		const auto adjacent = branch.battleAdjacentUnits(current);
		for(const auto & record : draft.unit(id)->effects->values())
			if(record.kind == forecastEffects::Kind::Bind && record.value->parameters)
			{
				const auto * source = branch.battleGetUnitByID(record.value->parameters->toNumber());
				observations.push_back({record.id, source && vstd::contains(adjacent, source)});
			}
	}
	const auto receipt = accepted(forecastEffects::Transitions::prepareRemoval(
		draft.unit(id)->effects, reason, acceptedEvent, observations));
	NativePreparation preparation(branch);
	auto target = preparation.unit(id);
	require(target != nullptr);
	auto staged = prepareEffectDraft(*target);
	require(stageEffects(draft, branch, id, receipt, *staged));
	refreshReadViews(draft, branch, std::span(&id, 1), false);
	preparation.accept(std::span(&id, 1));
	publishEffects(branch, std::move(draft), *target, *staged);
	return true;
}
forecastEffects::Receipt NativeAdapter::prepareMovementEffects(const State & source, HypotheticBattle & branch,
	UnitId id, forecastEffects::Removal route, bool actuallyMoved)
{
	if(route != forecastEffects::Removal::TargetMoved && route != forecastEffects::Removal::RootPreservingRelocation)
		throw std::invalid_argument("Movement requires an explicit native relocation route");
	require(source.unit(id) && source.unit(id)->effects);
	return accepted(forecastEffects::Transitions::prepareRemoval(source.unit(id)->effects,
		route, actuallyMoved, {}, branch.searchBudget().get()));
}
forecastEffects::Receipt NativeAdapter::prepareRoundEffects(const State & source, UnitId id,
	bool openingRound, int32_t nextRound, battleSearch::SearchBudget * budget)
{
	require(source.unit(id) && source.unit(id)->effects);
	return accepted(forecastEffects::Transitions::prepareRound(source.unit(id)->effects, openingRound, nextRound, budget));
}
bool NativeAdapter::stageRoundEffects(State & draft, HypotheticBattle & branch, UnitId id,
	const forecastEffects::Receipt & receipt, StackWithBonuses & staged)
{
	require(receipt.before && receipt.after && receipt.before->round() + 1 == receipt.after->round());
	applyNativeEffects(staged, receipt);
	staged.afterNewRound(receipt.before->round() == 0, true);
	// Round reset itself changes queue/resources even when no timer expires.
	auto changes = receipt;
	changes.dirty = changes.dirty | forecastEffects::Dirty::Queue;
	return stageEffects(draft, branch, id, changes, staged, nullptr, true, true);
}
forecastEffects::Receipt NativeAdapter::prepareActivationEffects(const State & source, HypotheticBattle & branch,
	UnitId id, BattleUnitTurnReason reason, bool deferred)
{
	const auto * target = branch.battleGetUnitByID(id);
	require(target && source.unit(id) && source.unit(id)->effects);
	const auto & effects = source.unit(id)->effects;
	forecastEffects::Activation input;
	input.reason = reason;
	input.beginsActivation = branch.battleBeginsActivation(target, reason);
	input.normalSlotForfeit = newHorizonsFrozen::forfeitsNormalActivation(*target, reason);
	input.deferFrozenThaw = deferred;
	input.alive = target->alive(); input.ghost = target->isGhost();
	// Branch-owned event identity, not the immutable live callback serial.
	require(effects->revision() < std::numeric_limits<uint64_t>::max());
	input.serial = effects->revision() + 1;
	return accepted(forecastEffects::Transitions::prepareActivation(effects, input, branch.searchBudget().get()));
}
forecastEffects::Receipt NativeAdapter::prepareActivationEndEffects(const State & source, HypotheticBattle & branch, UnitId id)
{
	const auto * target = branch.battleGetUnitByID(id);
	require(target && source.unit(id) && source.unit(id)->effects);
	const auto & effects = source.unit(id)->effects;
	const auto pending = effects->pendingForfeit();
	return accepted(forecastEffects::Transitions::prepareActivationEnd(effects,
		pending ? pending->activation : effects->revision(), target->alive(), target->isGhost(), branch.searchBudget().get()));
}
bool NativeAdapter::effectStrike(HypotheticBattle & branch, UnitId id, battle::timedEffects::StrikeClock clock)
{
	if(!branch.compactForecast) return false;
	auto draft = *branch.compactForecast;
	const auto receipt = accepted(forecastEffects::Transitions::prepareStrike(draft.unit(id)->effects,
		clock, branch.searchBudget().get()));
	NativePreparation preparation(branch);
	auto target = preparation.unit(id);
	require(target != nullptr);
	auto staged = prepareEffectDraft(*target);
	require(stageEffects(draft, branch, id, receipt, *staged));
	refreshReadViews(draft, branch, std::span(&id, 1), false);
	preparation.accept(std::span(&id, 1));
	publishEffects(branch, std::move(draft), *target, *staged);
	return true;
}
bool NativeAdapter::expireEffectSelector(HypotheticBattle & branch, UnitId id, const CSelector & selector)
{
	if(!branch.compactForecast) return false;
	const auto * current = branch.compactForecast->unit(id);
	require(current && current->effects);
	if(branch.searchBudget()) branch.searchBudget()->require(battleSearch::WorkKind::Node,
		static_cast<uint32_t>(current->effects->values().size() + 1));
	std::vector<Bonus> removed;
	for(const auto & record : current->effects->values())
		if(selector(record.value.get())) removed.push_back(*record.value);
	if(removed.empty()) return true; // proven no event effect: no native graph query
	NativePreparation preparation(branch);
	const auto target = preparation.unit(id);
	std::vector<Bonus> unmatched = removed;
	for(const auto & row : *target->getUnstackedBonuses(selector))
	{
		const auto found = std::ranges::find_if(unmatched, [&](const Bonus & value)
		{ return row && battle::exactBattleEffectEqual(value, *row); });
		if(found == unmatched.end()) throw std::invalid_argument("Expiry selected an unowned source clock");
		unmatched.erase(found);
	}
	if(!unmatched.empty()) throw std::invalid_argument("Expiry native/owned source multiset diverged");
	const bool committed = effectApplication(branch, id, removed, {});
	if(committed) preparation.accept(std::span(&id, 1));
	return committed;
}

}
