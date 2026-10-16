/*
 * StackWithBonusesEffects.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticEffectSnapshotRules.h"
#include "HypotheticLifecycleTimingScope.h"

#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsEnchantedCommand.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/NewHorizonsSorcery.h"

#include <unordered_map>

using hypotheticLifecycleDiagnostics::LifecycleTimingScope;
using hypotheticEffectSnapshotRules::isCapturedProjectionEffect;
using hypotheticEffectSnapshotRules::timedProjectionEffect;

namespace
{
bool isActivationClockPlague(const Bonus * bonus)
{
	static const SpellID plague(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	return bonus && bonus->source == BonusSource::SPELL_EFFECT
		&& bonus->sid == BonusSourceID(plague) && bonus->type == BonusType::COMBAT_EVENT_TRIGGER;
}

bool isGuardianSpiritBonus(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::GUARDIAN_SPIRIT;
}

const CSelector & guardianSpiritSelector()
{
	static const CSelector selector([](const Bonus * bonus)
	{
		return isGuardianSpiritBonus(bonus);
	});
	return selector;
}

void applyGuardianSpiritBonus(StackWithBonuses & unit, const Bonus & bonus)
{
	if(!isGuardianSpiritBonus(&bonus))
		return;

	const int32_t rounds = Bonus::NTurns(&bonus) ? std::max<int32_t>(0, bonus.turnsRemain) : 0;
	unit.guardianSpiritHitPoints = rounds > 0 ? std::max<int64_t>(0, bonus.val) : 0;
	unit.guardianSpiritRoundsRemaining = rounds;
}

void applyGuardianSpiritBonuses(StackWithBonuses & unit, const std::vector<Bonus> & bonuses)
{
	for(const auto & bonus : bonuses)
		applyGuardianSpiritBonus(unit, bonus);
}

void replacePhysicalAfflictionMarker(StackWithBonuses & unit, Bonus marker)
{
	physicalAfflictions::markerMetadata(marker);
	const CSelector selector([&marker](const Bonus * bonus)
	{
		return bonus && bonus->type == BonusType::PHYSICAL_AFFLICTION
			&& bonus->source == marker.source && bonus->sid == marker.sid;
	});

	const auto current = unit.getBonuses(selector);
	if(current && !current->empty())
	{
		for(const auto & existing : *current)
			if(existing && existing->duration == marker.duration && Bonus::NTurns(existing.get()))
				marker.turnsRemain = std::max(marker.turnsRemain, existing->turnsRemain);
		unit.removeUnitBonus(selector);
	}
	unit.bonusesToAdd.push_back(std::make_shared<Bonus>(std::move(marker)));
}

void restoreGuardianSpiritFromExistingBonuses(StackWithBonuses & unit)
{
	const auto bonuses = unit.getBonuses(guardianSpiritSelector());
	const Bonus * strongest = nullptr;
	for(const auto & bonus : *bonuses)
		if(bonus && (!strongest || bonus->val > strongest->val))
			strongest = bonus.get();

	if(!strongest)
	{
		unit.guardianSpiritHitPoints = 0;
		unit.guardianSpiritRoundsRemaining = 0;
		return;
	}

	const auto rounds = Bonus::NTurns(strongest)
		? std::max<int32_t>(0, strongest->turnsRemain) : 0;
	unit.guardianSpiritRoundsRemaining = rounds;
	if(rounds == 0)
		unit.guardianSpiritHitPoints = 0;
}

}

void StackWithBonuses::addUnitBonus(const std::vector<Bonus> & bonus)
{
	int swiftMarkers = 0;
	for(const auto & entry : bonus)
		if(newHorizonsSwiftRebirth::isLifecycleMarker(entry))
		{
			if(++swiftMarkers > 1)
				throw std::invalid_argument("Duplicate Swift Rebirth ADD");
			newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*this), entry, true);
		}
	captureLocalSpellEffects();
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	// The original bearer is live-backed. Snapshot this temporary perk effect
	// before adding its local forecast, so later live acceptance/expiry cannot
	// enter the branch a second time or change its independent activation life.
	if(std::any_of(bonus.begin(), bonus.end(), [](const Bonus & entry)
	{
		return newHorizonsEnchantedCommand::isMoraleBonus(&entry)
			|| newHorizonsDivineMandate::isSharedPurposeMoraleBonus(&entry);
	}))
		captureEffects();
	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*this, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*this, bonus);
	for(const auto & stamped : stampedBonuses)
		if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			capturedPhysicalAfflictionGroups.emplace(stamped.source, stamped.sid);
	for(const auto & stamped : stampedBonuses)
	{
		if(stamped.type == BonusType::CONFUSION_PENDING)
		{
			if(isTimeStopped())
				continue;
			auto next = confusionState;
			next.applyPending(stamped.spellCasterOwner, stamped.val == 2);
			removeUnitBonus(Selector::type()(BonusType::CONFUSION_PENDING));
			bonusesToAdd.push_back(std::make_shared<Bonus>(stamped));
			confusionState = next;
		}
		else if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			replacePhysicalAfflictionMarker(*this, stamped);
		else
			bonusesToAdd.push_back(std::make_shared<Bonus>(stamped));
		if(stamped.source == BonusSource::SPELL_EFFECT)
			exactLocalSpellEffects->push_back(stamped);
	}
	applyGuardianSpiritBonuses(*this, stampedBonuses);
	if(frozenApplication)
		commitPreparedFrozenApplication(*frozenApplication);
	treeVersionLocal++;
}

void StackWithBonuses::updateUnitBonus(const std::vector<Bonus> & bonus)
{
	int swiftMarkers = 0;
	for(const auto & entry : bonus)
		if(newHorizonsSwiftRebirth::isLifecycleMarker(entry))
		{
			if(++swiftMarkers > 1)
				throw std::invalid_argument("Duplicate Swift Rebirth UPDATE");
			newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*this), entry, false);
		}
	captureLocalSpellEffects();
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*this, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*this, bonus);
	// Preserve operation order: a preceding local ADD must be visible to refresh.
	captureEffects();
	for(const auto & stamped : stampedBonuses)
		if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			capturedPhysicalAfflictionGroups.emplace(stamped.source, stamped.sid);
	for(const auto & stamped : stampedBonuses)
	{
		if(newHorizonsSwiftRebirth::isLifecycleMarker(stamped))
		{
			removeUnitBonus(CSelector([](const Bonus * candidate)
			{
				return newHorizonsSwiftRebirth::isLifecycleMarker(*candidate);
			}));
			bonusesToAdd.push_back(std::make_shared<Bonus>(stamped));
		}
		else if(stamped.type == BonusType::CONFUSION_PENDING)
		{
			if(isTimeStopped())
				continue;
			auto next = confusionState;
			next.applyPending(stamped.spellCasterOwner, stamped.val == 2);
			removeUnitBonus(Selector::type()(BonusType::CONFUSION_PENDING));
			bonusesToAdd.push_back(std::make_shared<Bonus>(stamped));
			confusionState = next;
		}
		else if(stamped.type == BonusType::PHYSICAL_AFFLICTION)
			replacePhysicalAfflictionMarker(*this, stamped);
		else
			bonusesToUpdate.push_back(std::make_shared<Bonus>(stamped));
		if(stamped.source == BonusSource::SPELL_EFFECT)
		{
			bool found = false;
			for(auto & local : *exactLocalSpellEffects)
				if(local.source == stamped.source && local.sid == stamped.sid && local.type == stamped.type
					&& local.subtype == stamped.subtype && local.valType == stamped.valType)
				{
					local.turnsRemain = std::max(local.turnsRemain, stamped.turnsRemain);
					found = true;
				}
			if(!found)
				exactLocalSpellEffects->push_back(stamped);
		}
	}
	applyGuardianSpiritBonuses(*this, stampedBonuses);
	if(frozenApplication)
		commitPreparedFrozenApplication(*frozenApplication);
	treeVersionLocal++;
}

void StackWithBonuses::removeUnitBonus(const std::vector<Bonus> & bonus)
{
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	for(auto & one : bonus)
	{
		CSelector selector([&one](const Bonus * b) -> bool
		{
			//compare everything but turnsRemain, limiter and propagator
			return one.duration == b->duration
				&& one.type == b->type
				&& one.subtype == b->subtype
				&& one.source == b->source
				&& one.val == b->val
				&& one.sid == b->sid
				&& one.valType == b->valType
				&& one.effectRange == b->effectRange
				&& (one.type != BonusType::CONFUSION_PENDING || one.spellCasterOwner == b->spellCasterOwner);
		});

		removeUnitBonus(selector);
	}
}

bool StackWithBonuses::applyPurifySelection(const std::vector<SpellID> & spellEffectGroups,
	bool clearPhysicalPoisonState, const bool applyPurifyingMandate)
{
	bool changed = false;
	bool removedMagicalSpellGroup = false;
	bool removePhysicalPoison = clearPhysicalPoisonState;
	for(const auto sourceSpell : spellEffectGroups)
	{
		if(sourceSpell == newHorizonsPurify::physicalPoisonChoiceID())
		{
			removePhysicalPoison = true;
			continue;
		}

		auto group = newHorizonsPurify::spellEffectGroupBonuses(this, sourceSpell);
		if(group.empty())
			continue;

		removedMagicalSpellGroup = newHorizonsPurify::isMagicalSpellEffectGroup(this, sourceSpell)
			|| removedMagicalSpellGroup;
		removeUnitBonus(group);
		changed = true;
	}
	if(removePhysicalPoison)
		changed = newHorizonsPurify::clearPhysicalPoison(this) || changed;
	if(applyPurifyingMandate && removedMagicalSpellGroup)
		changed = removeFirstPhysicalAffliction() || changed;
	return changed;
}

bool StackWithBonuses::removeFirstPhysicalAffliction()
{
	const auto affliction = physicalAfflictions::first(*this);
	if(!affliction)
		return false;

	if(affliction->storedPoison)
		return newHorizonsPurify::clearPhysicalPoison(this);

	const auto removal = physicalAfflictions::removalPlan(*this, *affliction);
	if(removal.empty())
		return false;

	removeUnitBonus(removal);
	return true;
}

void StackWithBonuses::removeUnitBonus(const CSelector & selector)
{
	removeUnitBonus(selector, nullptr);
}

void StackWithBonuses::removeUnitBonus(const CSelector & selector,
	HypotheticLifecycleTiming::EffectWorkTiming * timing)
{
	removeUnitBonusImpl(selector, true, timing);
}

void StackWithBonuses::removeGamblerLuckPenalty()
{
	removeGamblerLuckPenalty(nullptr);
}

void StackWithBonuses::removeGamblerLuckPenalty(HypotheticLifecycleTiming::EffectWorkTiming * timing)
{
	// This exact Luck/Secondary Skill predicate cannot affect Confusion,
	// Guardian Spirit or Polymorph control state.
	removeUnitBonusImpl(CSelector(newHorizonsCombatSkills::isGamblerLuckPenalty), false, timing);
}

void StackWithBonuses::removeUnitBonusImpl(const CSelector & selector, bool mayAffectControlState,
	HypotheticLifecycleTiming::EffectWorkTiming * timing)
{
	captureLocalSpellEffects();
	const bool timeStopped = mayAffectControlState && isTimeStopped();
	const CSelector effectiveSelector([&selector, timeStopped](const Bonus * bonus)
	{
		return selector(bonus) && !(timeStopped && bonus
			&& (bonus->type == BonusType::CONFUSION_PENDING || battle::isPolymorphMarker(bonus)));
	});
	bool removesConfusion = false;
	bool removesGuardianSpirit = false;
	if(mayAffectControlState)
	{
		const auto confusionMarkers = getBonuses(Selector::type()(BonusType::CONFUSION_PENDING));
		removesConfusion = std::ranges::any_of(*confusionMarkers, [&](const auto & marker)
		{
			return newHorizonsConfusionControl::isPendingMarker(marker.get()) && effectiveSelector(marker.get());
		});
		const auto guardianSpiritBonuses = getBonuses(guardianSpiritSelector());
		removesGuardianSpirit = std::ranges::any_of(*guardianSpiritBonuses,
			[&](const auto & bonus) { return bonus && effectiveSelector(bonus.get()); });
	}

	// Parent models materialize fresh bonus pointers. Capture effect values before
	// suppressing them, including non-timed spells and legacy battle-long effects.
	captureEffects(timing);
	TConstBonusListPtr toRemove;
	{
		LifecycleTimingScope measurement(timing ? &timing->selectiveRemovalQuery : nullptr);
		toRemove = origBearer->getBonuses(effectiveSelector);
	}
	LifecycleTimingScope mutation(timing ? &timing->removalMutation : nullptr);

	for(auto b : *toRemove)
		bonusesToRemove.insert(b);

	vstd::erase_if(bonusesToAdd, [&](const auto & b){return effectiveSelector(b.get());});
	vstd::erase_if(bonusesToUpdate, [&](const auto & b){return effectiveSelector(b.get());});
	if(projectedEffects)
		vstd::erase_if(*projectedEffects, [&](const Bonus & b){return effectiveSelector(&b);});
	vstd::erase_if(*exactLocalSpellEffects, [&](const Bonus & b){return effectiveSelector(&b);});
	if(projectedUnstackedEffects)
		vstd::erase_if(*projectedUnstackedEffects, [&](const std::shared_ptr<Bonus> & b)
		{
			return !b || effectiveSelector(b.get());
		});
	++treeVersionLocal;
	if(removesConfusion && !hasBonusOfType(BonusType::CONFUSION_PENDING))
		confusionState.clearPending();
	if(removesGuardianSpirit)
		restoreGuardianSpiritFromExistingBonuses(*this);
}

void StackWithBonuses::freezeDamageScoringBonusSnapshot()
{
	if(damageScoringBonuses)
		return;

	const auto stacked = getBonusesBeforeCreatureAbilitySuppression(Selector::all, {}, false);
	const auto unstacked = getBonusesBeforeCreatureAbilitySuppression(Selector::all, {}, true);
	damageScoringBonuses.emplace();
	damageScoringBonuses->reserve(stacked->size());
	for(const auto & bonus : *stacked)
		if(bonus)
			damageScoringBonuses->push_back(*bonus);
	damageScoringUnstackedBonuses.emplace();
	damageScoringUnstackedBonuses->reserve(unstacked->size());
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> copies;
	for(const auto & bonus : *unstacked)
		if(bonus)
		{
			auto [copy, inserted] = copies.try_emplace(bonus.get());
			if(inserted)
				copy->second = std::make_shared<Bonus>(*bonus);
			damageScoringUnstackedBonuses->push_back(copy->second);
		}
}

void StackWithBonuses::applyNoQuarter(int32_t moraleActivationsRemaining, bool appliedByEnemy)
{
	if(moraleActivationsRemaining <= 0 || moraleActivationsRemaining > 2)
		throw std::invalid_argument("No Quarter morale duration must be one or two activations");

	removeUnitBonus(CSelector([](const Bonus * bonus)
	{
		return newHorizonsOffense::isNoQuarterBonus(bonus);
	}));
	addUnitBonus({
		newHorizonsOffense::noQuarterRetaliationBonus(),
		newHorizonsOffense::noQuarterMoralePenalty(appliedByEnemy)});
	noQuarterMoraleActivationsRemaining = moraleActivationsRemaining;
}

void StackWithBonuses::consumeNoQuarterActivation()
{
	if(noQuarterMoraleActivationsRemaining <= 0)
		return;

	--noQuarterMoraleActivationsRemaining;
	if(noQuarterMoraleActivationsRemaining == 0)
		removeUnitBonus(CSelector([](const Bonus * bonus)
		{
			return newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
		}));
}

void StackWithBonuses::clearNoQuarterRoundBlocker()
{
	removeUnitBonus(CSelector([](const Bonus * bonus)
	{
		return newHorizonsOffense::isNoQuarterRetaliationBonus(bonus);
	}));
}

std::vector<Bonus> StackWithBonuses::localSpellEffects() const
{
	if(exactLocalSpellEffects)
		return *exactLocalSpellEffects;
	if(const auto * parent = dynamic_cast<const StackWithBonuses *>(origBearer))
		return parent->localSpellEffects();
	const auto * node = dynamic_cast<const CBonusSystemNode *>(origBearer);
	if(!node)
		throw std::runtime_error("Detached exact effect capture requires source-local bonus provenance");
	std::vector<Bonus> result;
	for(const auto & bonus : node->getExportedBonusList())
		if(bonus->source == BonusSource::SPELL_EFFECT)
			result.push_back(*bonus);
	return result;
}

void StackWithBonuses::captureLocalSpellEffects()
{
	if(!exactLocalSpellEffects)
		exactLocalSpellEffects = localSpellEffects();
}

void StackWithBonuses::replaceLocalSpellEffectsExact(const std::vector<Bonus> & replacement)
{
	captureLocalSpellEffects();
	captureEffects();
	// The raw snapshot includes inherited spell effects. Remove exactly the
	// source-local multiset, not every effect sharing a spell/source identity.
	for(const auto & effect : *exactLocalSpellEffects)
	{
		const auto found = std::find_if(projectedUnstackedEffects->begin(), projectedUnstackedEffects->end(),
			[&effect](const auto & bonus) { return battle::exactBattleEffectEqual(*bonus, effect); });
		if(found == projectedUnstackedEffects->end())
			throw std::runtime_error("Cannot stage source-local effect in detached raw snapshot");
		projectedUnstackedEffects->erase(found);
	}
	for(const auto & effect : replacement)
		projectedUnstackedEffects->push_back(std::make_shared<Bonus>(effect));
	BonusList stacked;
	for(const auto & bonus : *projectedUnstackedEffects)
		stacked.push_back(bonus);
	stacked.stackBonuses();
	projectedEffects->clear();
	for(const auto & bonus : stacked)
		projectedEffects->push_back(*bonus);
	exactLocalSpellEffects = replacement;
	damageScoringBonuses.reset();
	damageScoringUnstackedBonuses.reset();
	++treeVersionLocal;
}

void StackWithBonuses::captureEffects()
{
	captureEffects(nullptr);
}

void StackWithBonuses::captureEffects(HypotheticLifecycleTiming::EffectWorkTiming * timing)
{
	// Resolve refreshes before aging or another mutation. In addition to spell and
	// command effects, explicitly marked affliction groups may use non-spell
	// sources; capture only those exact source/sid groups, not unrelated stats.
	TConstBonusListPtr effects;
	{
		LifecycleTimingScope measurement(timing ? &timing->stackedQuery : nullptr);
		effects = getAllBonuses(Selector::all);
	}
	TConstBonusListPtr unstackedEffects;
	{
		LifecycleTimingScope measurement(timing ? &timing->rawQuery : nullptr);
		unstackedEffects = getUnstackedBonuses(Selector::all);
	}
	LifecycleTimingScope materialization(timing ? &timing->snapshotMaterialization : nullptr);
	if(projectedEffects)
		projectedEffects->clear();
	else
		projectedEffects.emplace();
	if(projectedUnstackedEffects)
		projectedUnstackedEffects->clear();
	else
		projectedUnstackedEffects.emplace();
	for(const auto & bonus : *unstackedEffects)
		if(bonus && bonus->type == BonusType::PHYSICAL_AFFLICTION)
		{
			physicalAfflictions::markerMetadata(*bonus);
			capturedPhysicalAfflictionGroups.emplace(bonus->source, bonus->sid);
		}
	for(const auto & bonus : *effects)
		if(bonus && isCapturedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups))
			projectedEffects->push_back(*bonus);
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> rawCopies;
	for(const auto & bonus : *unstackedEffects)
		if(bonus && isCapturedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups))
		{
			auto [copy, inserted] = rawCopies.try_emplace(bonus.get());
			if(inserted)
				copy->second = std::make_shared<Bonus>(*bonus);
			projectedUnstackedEffects->push_back(copy->second);
		}
	vstd::erase_if(bonusesToAdd, [this](const auto & bonus)
	{
		return isCapturedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups);
	});
	vstd::erase_if(bonusesToUpdate, [this](const auto & bonus)
	{
		return isCapturedProjectionEffect(bonus.get(), capturedPhysicalAfflictionGroups);
	});
}

void StackWithBonuses::advanceTimedRound()
{
	advanceTimedRound(nullptr);
}

void StackWithBonuses::advanceTimedRound(HypotheticLifecycleTiming::EffectWorkTiming * timing)
{
	captureLocalSpellEffects();
	captureEffects(timing);
	if(isTimeStopped())
	{
		// Time Stop takes the projected unit outside the round clock.  Keep the
		// captured effect identities (so later hypothetical removals still see
		// them), but do not age or expire any N_TURNS effect.
		++treeVersionLocal;
		return;
	}
	LifecycleTimingScope aging(timing ? &timing->roundAging : nullptr);
	// Read the seal and polarity before its own final round expires. These
	// immutable values apply equally to stacked, source-local and raw aliases.
	const auto preservedPolarity = newHorizonsSorcery::spellLockPreservesBeneficial(*this);
	const bool sealed = preservedPolarity.has_value() && newHorizonsSorcery::isSpellLocked(*this);
	const auto roundTimed = [this, sealed, preservedPolarity](const Bonus * bonus)
	{
		return timedProjectionEffect(bonus, capturedPhysicalAfflictionGroups)
			&& !isActivationClockPlague(bonus)
			&& !(sealed && newHorizonsSorcery::isPreservedSpellLockEffect(bonus, *preservedPolarity));
	};
	const auto age = [&roundTimed](std::vector<Bonus> & bonuses)
	{
		for(auto & bonus : bonuses)
			if(roundTimed(&bonus) && bonus.turnsRemain > 0)
				--bonus.turnsRemain;
		vstd::erase_if(bonuses, [&roundTimed](const Bonus & bonus)
		{
			return roundTimed(&bonus) && bonus.turnsRemain <= 0;
		});
	};
	age(*projectedEffects);
	age(*exactLocalSpellEffects);
	if(projectedUnstackedEffects)
	{
		// Raw snapshots may contain the same inherited Bonus pointer multiple
		// times. Clone each expiring identity once, reconnect all aliases, and age
		// that branch-local clone once so parent/sibling projections stay intact.
		std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> branchCopies;
		for(auto & bonus : *projectedUnstackedEffects)
		{
			if(!bonus || !roundTimed(bonus.get())
				|| bonus->turnsRemain <= 0)
				continue;
			auto [copy, inserted] = branchCopies.try_emplace(bonus.get());
			if(inserted)
				copy->second = std::make_shared<Bonus>(*bonus);
			bonus = copy->second;
		}
		for(const auto & copy : branchCopies)
			if(copy.second->turnsRemain > 0)
				--copy.second->turnsRemain;
		vstd::erase_if(*projectedUnstackedEffects, [&roundTimed](const std::shared_ptr<Bonus> & bonus)
		{
			return !bonus || (roundTimed(bonus.get())
				&& bonus->turnsRemain <= 0);
		});
	}
	const auto ageRecords = [&roundTimed](auto & records)
	{
		for(auto & record : records)
			if(roundTimed(record.get()) && record->turnsRemain > 0)
			{
				// A retained query/descendant may still own the earlier identity.
				record = std::make_shared<Bonus>(*record);
				--record->turnsRemain;
			}
		vstd::erase_if(records, [&roundTimed](const auto & record)
		{
			return roundTimed(record.get()) && record->turnsRemain <= 0;
		});
	};
	ageRecords(bonusesToAdd);
	ageRecords(bonusesToUpdate);
	++treeVersionLocal;
	restoreGuardianSpiritFromExistingBonuses(*this);
	if(health.isCapacityHealthTracking() && getCapacityHealthReferenceMax() > 0)
	{
		// Match the authoritative round-expiry hook for Hydra's Vitality. Timed
		// capacity effects disappear inside round aging rather than via a bonus
		// pack, so clamp each survivor before exposing the projected state.
		static const SpellID hydrasVitalitySpell(SpellID::decode("new-horizons:hydrasVitality"));
		const auto capacitySelector = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::STACK_HEALTH));
		const auto regenerationSelector = Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(hydrasVitalitySpell)).And(Selector::type()(BonusType::HP_REGENERATION));
		normalizeCapacityHealth();
		if(!hasBonus(capacitySelector) && !hasBonus(regenerationSelector))
			clearCapacityHealthReference();
	}
}
