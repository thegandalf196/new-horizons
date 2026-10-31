/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "GraveDustEffectChanges.h"
#include "GraveDustNativePreflight.h"
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
#include "StackEffectRecordRules.h"
#include "StackEffectVectorRules.h"
#include "StackEffectRefresh.h"
#include "TimeStopState.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include <algorithm>
#include <stdexcept>

void BattleInfo::addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	stackEffectVectorRules::validateIncomingRecords(*this, id, bonus, [this](uint32_t target)
	{
		return getUnitsIf([target](const battle::Unit * unit) { return unit && unit->unitId() == target; });
	});
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	newHorizonsGraveDust::validateNativeMutation(*this, *sta, bonus, newHorizonsGraveDust::EffectOperation::ADD);
	stackEffectVectorRules::validateSwiftRecords(*sta, bonus, true);

	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*sta, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*sta, bonus);
	for(const Bonus & b : stampedBonuses)
		addOrUpdateUnitBonus(sta, b, true);
	if(frozenApplication)
		sta->commitPreparedFrozenApplication(*frozenApplication);
}

void BattleInfo::updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	stackEffectVectorRules::validateIncomingRecords(*this, id, bonus, [this](uint32_t target)
	{
		return getUnitsIf([target](const battle::Unit * unit) { return unit && unit->unitId() == target; });
	});
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	newHorizonsGraveDust::validateNativeMutation(*this, *sta, bonus, newHorizonsGraveDust::EffectOperation::UPDATE);
	stackEffectVectorRules::validateSwiftRecords(*sta, bonus, false);

	const auto stampedBonuses = physicalAfflictions::stampApplicationOrder(*sta, bonus);
	const auto frozenApplication = newHorizonsFrozen::prepareApplication(*sta, bonus);
	for(const Bonus & b : stampedBonuses)
		addOrUpdateUnitBonus(sta, b, false);
	if(frozenApplication)
		sta->commitPreparedFrozenApplication(*frozenApplication);
}

void BattleInfo::removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	stackEffectVectorRules::validateRemovalRecords(bonus);
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	newHorizonsGraveDust::validateNativeMutation(*this, *sta, bonus, newHorizonsGraveDust::EffectOperation::REMOVE);

	for(const Bonus & one : bonus)
	{
		if(stackEffectRecordRules::removalProtected(*sta, one))
		{
			logNetwork->warn("Ignoring effect removal from Time Stop unit %d", id);
			continue;
		}
		if(stackEffectRecordRules::selectsCurrentFormMarker(*sta, one)
			&& !battle::endBattleFormAtNearestLegalPosition(*sta, getAccessibility(sta)))
			continue; // Retain both marker and form until a legal round boundary.

		auto selector = [one](const Bonus * b)
		{
			return battle::matchesStackEffectRemoval(one, *b);
		};
		const bool removesConfusion = stackEffectRecordRules::removesConfusion(*sta, one);
		sta->removeBonusesRecursive(selector);
		stackEffectRecordRules::finishConfusionRemoval(*sta, removesConfusion);
		if(one.type == BonusType::GUARDIAN_SPIRIT)
			stackEffectRecordRules::restoreGuardianAfterRemoval(*sta);
	}
}

void BattleInfo::addOrUpdateUnitBonus(CStack * sta, const Bonus & value, bool forceAdd)
{
	if(newHorizonsSwiftRebirth::isLifecycleMarker(value))
	{
		const auto previous = stackEffectRecordRules::previousSwiftLifecycle(*sta, value, forceAdd, sta->getExportedBonusList());
		if(previous)
			sta->removeBonus(previous);
		sta->addNewBonus(std::make_shared<Bonus>(value));
		return; // Lifecycle bookkeeping remains writable during Time Stop.
	}
	if(value.type == BonusType::CONFUSION_PENDING)
		newHorizonsConfusionControl::validateMarker(value);
	if(value.type == BonusType::PHYSICAL_AFFLICTION)
		physicalAfflictions::markerMetadata(value);

	if(stackEffectRecordRules::incomingProtected(*sta, value))
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
		const auto replacement = stackEffectRecordRules::physicalMarkerReplacement(value, sta->getExportedBonusList());
		if(replacement.replaceExisting)
			sta->removeBonusesRecursive(stackEffectRecordRules::physicalMarkerGroup(value));
		sta->addNewBonus(std::make_shared<Bonus>(replacement.marker));
		return;
	}
	if(value.type == BonusType::GUARDIAN_SPIRIT)
	{
		const auto pool = stackEffectRecordRules::guardianPool(value);
		sta->removeBonusesRecursive(Selector::type()(BonusType::GUARDIAN_SPIRIT));
		sta->addNewBonus(std::make_shared<Bonus>(value));
		sta->guardianSpiritHitPoints = pool.hitPoints;
		sta->guardianSpiritRoundsRemaining = pool.rounds;
		return;
	}

	const bool hasLegacyRefreshCandidate = sta->hasBonus(stackEffectRefresh::legacyCandidateSelector(value));
	const auto refresh = stackEffectRefresh::plan(forceAdd, value, sta->getExportedBonusList(), hasLegacyRefreshCandidate);
	if(refresh.append)
	{
		if(refresh.identified)
			logBonus->trace("%s receives a new identified status bonus: %s", sta->nodeName(), value.Description(nullptr));
		else
			logBonus->trace("%s receives a new bonus: %s", sta->nodeName(), value.Description(nullptr));
		sta->addNewBonus(std::make_shared<Bonus>(value));
	}
	else
	{
		if(refresh.identified)
			logBonus->trace("%s updated identified status bonus: %s", sta->nodeName(), value.Description(nullptr));
		for(const auto & stackBonus : sta->getExportedBonusList()) //TODO: optimize
			if(stackEffectRefresh::matchesIdentity(value, stackBonus.get()))
				stackEffectRefresh::refreshFields(*stackBonus, value);
		sta->nodeHasChanged();
	}
}
