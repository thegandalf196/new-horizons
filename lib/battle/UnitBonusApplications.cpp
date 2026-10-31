/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "GraveDustEffectChanges.h"
#include "../CStack.h"
#include "AccessibilityInfo.h"
#include "BattleForm.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "NewHorizonsParalyzingGaze.h"
#include "NewHorizonsConfusionControl.h"
#include "NewHorizonsSwiftRebirth.h"
#include "NewHorizonsFrozen.h"
#include "NewHorizonsOffense.h"
#include "PhysicalAffliction.h"
#include "StackEffectRemoval.h"
#include "TimeStopState.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include <algorithm>
#include <stdexcept>

void BattleInfo::addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	for(const auto & entry : bonus) newHorizonsSerpentsEmbrace::validateApplication(*this, id, entry);
	for(const auto & entry : bonus) newHorizonsParalyzingGaze::validateApplication(*this, id, entry);
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	newHorizonsGraveDust::validateMutation(*this, *sta, bonus, newHorizonsGraveDust::EffectOperation::ADD);
	int swiftMarkers = 0;
	for(const auto & entry : bonus)
		if(newHorizonsSwiftRebirth::isLifecycleMarker(entry))
		{
			if(++swiftMarkers > 1)
				throw std::invalid_argument("Duplicate Swift Rebirth ADD");
			newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*sta), entry, true);
		}

	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*sta, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*sta, bonus);
	for(const Bonus & b : stampedBonuses)
		addOrUpdateUnitBonus(sta, b, true);
	if(frozenApplication)
		sta->commitPreparedFrozenApplication(*frozenApplication);
}

void BattleInfo::updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	for(const auto & entry : bonus) newHorizonsSerpentsEmbrace::validateApplication(*this, id, entry);
	for(const auto & entry : bonus) newHorizonsParalyzingGaze::validateApplication(*this, id, entry);
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	newHorizonsGraveDust::validateMutation(*this, *sta, bonus, newHorizonsGraveDust::EffectOperation::UPDATE);
	int swiftMarkers = 0;
	for(const auto & entry : bonus)
		if(newHorizonsSwiftRebirth::isLifecycleMarker(entry))
		{
			if(++swiftMarkers > 1)
				throw std::invalid_argument("Duplicate Swift Rebirth UPDATE");
			newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*sta), entry, false);
		}

	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*sta, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*sta, bonus);
	for(const Bonus & b : stampedBonuses)
		addOrUpdateUnitBonus(sta, b, false);
	if(frozenApplication)
		sta->commitPreparedFrozenApplication(*frozenApplication);
}

void BattleInfo::removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	for(const auto & entry : bonus)
		if(entry.type == BonusType::CONFUSION_PENDING)
			newHorizonsConfusionControl::validateMarker(entry);
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	newHorizonsGraveDust::validateMutation(*this, *sta, bonus, newHorizonsGraveDust::EffectOperation::REMOVE);

	for(const Bonus & one : bonus)
	{
		if(sta->isTimeStopped() && !timeStopState::isStateBonus(one)
			&& !newHorizonsOffense::isNoQuarterBonus(&one)
			&& !newHorizonsSerpentsEmbrace::isMarker(one))
		{
			logNetwork->warn("Ignoring effect removal from Time Stop unit %d", id);
			continue;
		}
		if(battle::isPolymorphMarker(&one) && sta->hasBattleForm())
		{
			const auto markers = sta->getBonuses(CSelector([&one](const Bonus * candidate)
			{
				return battle::isPolymorphMarker(candidate) && candidate->sid == one.sid
					&& candidate->val == one.val;
			}));
			if(markers && !markers->empty()
				&& !battle::endBattleFormAtNearestLegalPosition(*sta, getAccessibility(sta)))
				continue; // Retain both marker and form until a legal round boundary.
		}

		auto selector = [one](const Bonus * b)
		{
			return battle::matchesStackEffectRemoval(one, *b);
		};
		const auto confusionMarkers = sta->getBonuses(Selector::type()(BonusType::CONFUSION_PENDING));
		const bool removesConfusion = std::ranges::any_of(*confusionMarkers, [&](const auto & marker)
		{
			return newHorizonsConfusionControl::isPendingMarker(marker.get()) && selector(marker.get());
		});
		sta->removeBonusesRecursive(selector);
		if(removesConfusion && !sta->hasBonusOfType(BonusType::CONFUSION_PENDING))
			sta->confusionState.clearPending();
		if(one.type == BonusType::GUARDIAN_SPIRIT)
		{
			const auto remaining = sta->getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT));
			if(!remaining || remaining->empty())
			{
				sta->guardianSpiritHitPoints = 0;
				sta->guardianSpiritRoundsRemaining = 0;
			}
			else
			{
				sta->guardianSpiritRoundsRemaining = 0;
				for(const auto & marker : *remaining)
					if(marker)
						sta->guardianSpiritRoundsRemaining = std::max<int32_t>(
							sta->guardianSpiritRoundsRemaining, marker->turnsRemain);
			}
		}
	}
}

void BattleInfo::addOrUpdateUnitBonus(CStack * sta, const Bonus & value, bool forceAdd)
{
	if(newHorizonsSwiftRebirth::isLifecycleMarker(value))
	{
		newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(*sta), value, forceAdd);
		std::shared_ptr<Bonus> previous;
		for(const auto & existing : sta->getExportedBonusList())
			if(existing && newHorizonsSwiftRebirth::isLifecycleMarker(*existing))
				previous = existing;
		if(!forceAdd && !previous)
			throw std::invalid_argument("Swift Rebirth update requires a local lifecycle");
		if(previous)
			sta->removeBonus(previous);
		sta->addNewBonus(std::make_shared<Bonus>(value));
		return; // Lifecycle bookkeeping remains writable during Time Stop.
	}
	if(value.type == BonusType::CONFUSION_PENDING)
		newHorizonsConfusionControl::validateMarker(value);
	if(value.type == BonusType::PHYSICAL_AFFLICTION)
		physicalAfflictions::markerMetadata(value);

	if(sta->isTimeStopped() && !timeStopState::isStateBonus(value))
	{
		logNetwork->warn("Ignoring new effect on Time Stop unit %d", sta->unitId());
		return;
	}
	if(value.type == BonusType::CONFUSION_PENDING)
	{
		auto next = sta->confusionState;
		next.applyPending(value.spellCasterOwner, value.val == 2);
		sta->removeBonusesRecursive(Selector::type()(BonusType::CONFUSION_PENDING));
		sta->addNewBonus(std::make_shared<Bonus>(value));
		sta->confusionState = next;
		return;
	}
	if(value.type == BonusType::PHYSICAL_AFFLICTION)
	{
		// A source/sid pair owns one marker. Replace duplicate local markers while
		// retaining the longest same-duration timer, so repeated SetStackEffect
		// entries cannot leave multiple ordering records for one status group.
		Bonus marker = value;
		bool found = false;
		for(const auto & existing : sta->getExportedBonusList())
		{
			if(!existing || existing->type != BonusType::PHYSICAL_AFFLICTION
				|| existing->source != value.source || existing->sid != value.sid)
				continue;
			found = true;
			if(existing->duration == marker.duration && Bonus::NTurns(existing.get()))
				marker.turnsRemain = std::max(marker.turnsRemain, existing->turnsRemain);
		}
		if(found)
		{
			const CSelector markerGroup([&value](const Bonus * bonus)
			{
				return bonus && bonus->type == BonusType::PHYSICAL_AFFLICTION
					&& bonus->source == value.source && bonus->sid == value.sid;
			});
			sta->removeBonusesRecursive(markerGroup);
		}
		sta->addNewBonus(std::make_shared<Bonus>(marker));
		return;
	}
	if(value.type == BonusType::GUARDIAN_SPIRIT)
	{
		if(value.val <= 0 || !Bonus::NTurns(&value) || value.turnsRemain <= 0)
			throw std::runtime_error("Invalid Guardian Spirit bonus pool or duration");

		// Guardian Spirit is a refreshable pool, not a stackable bonus. The
		// marker carries the cast-time maximum while the saved unit state carries
		// the remaining pool consumed by physical damage.
		sta->removeBonusesRecursive(Selector::type()(BonusType::GUARDIAN_SPIRIT));
		sta->addNewBonus(std::make_shared<Bonus>(value));
		sta->guardianSpiritHitPoints = value.val;
		sta->guardianSpiritRoundsRemaining = value.turnsRemain;
		return;
	}

	const auto matchesRefreshIdentity = [&value](const std::shared_ptr<Bonus> & stackBonus)
	{
		return stackBonus && stackBonus->source == value.source && stackBonus->sid == value.sid
			&& stackBonus->type == value.type && stackBonus->subtype == value.subtype
			&& stackBonus->valType == value.valType && stackBonus->statusIdentity == value.statusIdentity;
	};
	const auto updateMatchingBonuses = [&]()
	{
		for(const auto & stackBonus : sta->getExportedBonusList()) //TODO: optimize
		{
			if(!matchesRefreshIdentity(stackBonus))
				continue;
			stackBonus->turnsRemain = std::max(stackBonus->turnsRemain, value.turnsRemain);
			for(const BonusStatusTag tag : value.statusTags)
				if(std::find(stackBonus->statusTags.begin(), stackBonus->statusTags.end(), tag) == stackBonus->statusTags.end())
					stackBonus->statusTags.push_back(tag);
		}
		sta->nodeHasChanged();
	};
	const CSelector matchingStatusIdentity([&value](const Bonus * candidate)
	{
		return candidate && candidate->statusIdentity == value.statusIdentity;
	});
	const bool hasLegacyRefreshCandidate = sta->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, value.sid)
		.And(Selector::typeSubtypeValueType(value.type, value.subtype, value.valType)).And(matchingStatusIdentity));
	if(!value.hasStatusMetadata())
	{
		// Preserve legacy selector behavior for ordinary untagged bonuses. The
		// local refresh still avoids merging into a distinct explicit identity.
		if(forceAdd || !hasLegacyRefreshCandidate)
		{
			logBonus->trace("%s receives a new bonus: %s", sta->nodeName(), value.Description(nullptr));
			sta->addNewBonus(std::make_shared<Bonus>(value));
		}
		else
			updateMatchingBonuses();
	}
	else
	{
		// Explicit statuses are keyed by the ordinary component fields plus their
		// identity. Different identities must coexist, including non-spell sources.
		const bool hasMatchingIdentity = std::ranges::any_of(sta->getExportedBonusList(), matchesRefreshIdentity);
		if(forceAdd || !hasMatchingIdentity)
		{
			logBonus->trace("%s receives a new identified status bonus: %s", sta->nodeName(), value.Description(nullptr));
			sta->addNewBonus(std::make_shared<Bonus>(value));
		}
		else
		{
			logBonus->trace("%s updated identified status bonus: %s", sta->nodeName(), value.Description(nullptr));
			updateMatchingBonuses();
		}
	}
}
