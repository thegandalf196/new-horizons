/*
 * StackBonusApplications.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "HypotheticBonusApplication.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsEnchantedCommand.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/battle/PhysicalAffliction.h"

namespace hypotheticBonusApplication
{
bool isGuardianSpiritBonus(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::GUARDIAN_SPIRIT;
}

}
using hypotheticBonusApplication::isGuardianSpiritBonus;
namespace
{
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

}
void StackWithBonuses::addUnitBonus(const std::vector<Bonus> & bonus)
{
	if(const auto subject = owner->initialExportSubject())
	{
		subject->addSequenceBonus(*owner, id, bonus, true);
		++treeVersionLocal;
		return;
	}
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
	if(const auto subject = owner->initialExportSubject())
	{
		subject->addSequenceBonus(*owner, id, bonus, false);
		++treeVersionLocal;
		return;
	}
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
