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
#include "../../lib/battle/StackEffectRemoval.h"
#include "StackWithBonuses.h"
#include "GraveDustProjection.h"
#include "../../lib/battle/GraveDustEffectChanges.h"
#include "HypotheticBonusApplication.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
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
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/HydrasVitalityIdentity.h"
#include "../../lib/spells/NewHorizonsSorcery.h"

#include <unordered_map>

using hypotheticLifecycleDiagnostics::LifecycleTimingScope;
using hypotheticEffectSnapshotRules::isCapturedProjectionEffect;
using hypotheticEffectSnapshotRules::timedProjectionEffect;
using hypotheticBonusApplication::isGuardianSpiritBonus;

namespace
{

const CSelector & guardianSpiritSelector()
{
	static const CSelector selector([](const Bonus * bonus)
	{
		return isGuardianSpiritBonus(bonus);
	});
	return selector;
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



void StackWithBonuses::removeUnitBonus(const std::vector<Bonus> & bonus)
{
	newHorizonsGraveDust::validateMutation(*owner, *this, bonus, newHorizonsGraveDust::EffectOperation::REMOVE);
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	for(auto & one : bonus)
	{
		CSelector selector([&one](const Bonus * b) -> bool
		{
			return battle::matchesStackEffectRemoval(one, *b);
		});

		removeUnitBonusImpl(selector, true, nullptr);
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
	graveDustAI::validateSelectorRemoval(*owner, *this, selector);
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
	const auto protectedRemoval = graveDustAI::protectedRemovalSelector(*this, selector);
	if(const auto subject = owner->initialExportSubject())
	{
		subject->remove(*owner, id, protectedRemoval);
		++treeVersionLocal;
		return;
	}
	captureLocalSpellEffects();
	const bool timeStopped = mayAffectControlState && isTimeStopped();
	const CSelector effectiveSelector([&protectedRemoval, timeStopped](const Bonus * bonus)
	{
		return protectedRemoval(bonus) && !(timeStopped && bonus
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
	const auto plagueMarker = newHorizonsPlague::markerSelector(owner->getMagicRules());
	const auto roundTimed = [this, sealed, preservedPolarity, &plagueMarker](const Bonus * bonus)
	{
		return timedProjectionEffect(bonus, capturedPhysicalAfflictionGroups)
			&& !(bonus && plagueMarker(bonus))
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
		const auto hydrasSource = newHorizonsMagic::hydrasVitalitySource(owner->getMagicRules());
		const auto capacitySelector = hydrasSource.And(Selector::type()(BonusType::STACK_HEALTH));
		const auto regenerationSelector = hydrasSource.And(Selector::type()(BonusType::HP_REGENERATION));
		normalizeCapacityHealth();
		if(!hasBonus(capacitySelector) && !hasBonus(regenerationSelector))
			clearCapacityHealthReference();
	}
}
