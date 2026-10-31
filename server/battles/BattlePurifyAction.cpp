/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattlePurifyAction.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CurePhysicalAffliction.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include <map>
#include <set>
namespace
{
void clearStoredPoison(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const std::vector<newHorizonsPurify::EligibleStack> & eligible,
	const std::set<int32_t> & manuallySelectedPhysicalPoison, std::set<uint32_t> & affectedRecipients)
{
	const auto battleId = battle.getBattle()->getBattleID();
	BattleUnitsChanged physicalPoisonUpdates;
	physicalPoisonUpdates.battleID = battleId;
	BattleLogMessage physicalPoisonLog;
	physicalPoisonLog.battleID = battleId;
	for(const auto & stack : eligible)
	{
		if(!manuallySelectedPhysicalPoison.contains(stack.unitId)
			&& !stack.physicalPoisonAutomaticallyCleared)
			continue;
		const auto * unit = battle.battleGetStackByID(stack.unitId, false);
		if(!unit)
			continue;

		auto state = unit->acquireState();
		if(!newHorizonsPurify::clearPhysicalPoison(state.get()))
			continue;

		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		physicalPoisonUpdates.changedStacks.push_back(std::move(update));

		MetaString line;
		line.appendRawString(stack.physicalPoisonAutomaticallyCleared
			? "Purifier removes physical Poison from %s."
			: "Purify removes physical Poison from %s.");
		unit->addNameReplacement(line, unit->getCount());
		physicalPoisonLog.lines.push_back(std::move(line));
	}

	if(!physicalPoisonUpdates.changedStacks.empty())
	{
		gameHandler.sendAndApply(physicalPoisonUpdates);
		for(const auto & change : physicalPoisonUpdates.changedStacks)
			if(const auto * unit = battle.battleGetStackByID(change.id, false);
				unit && unit->physicalPoisonActivationsRemaining == 0)
				affectedRecipients.insert(change.id);
	}
	if(!physicalPoisonLog.lines.empty())
		gameHandler.sendAndApply(physicalPoisonLog);
}
void clearGraveDust(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const std::vector<newHorizonsPurify::EligibleStack> & eligible, std::set<uint32_t> & affectedRecipients)
{
	SetStackEffect change;
	change.battleID = battle.getBattle()->getBattleID();
	for(const auto & stack : eligible)
	{
		if(!stack.graveDustAutomaticallyCleared) continue;
		const auto * unit = battle.battleGetUnitByID(stack.unitId);
		if(!unit) continue;
		auto removed = spells::curePhysical::graveDustRemoval(*unit);
		if(!removed.empty()) change.toRemove.emplace_back(unit->unitId(), std::move(removed));
	}
	if(change.toRemove.empty()) return;
	gameHandler.sendAndApply(change);
	BattleLogMessage message;
	message.battleID = change.battleID;
	for(const auto & entry : change.toRemove)
		if(const auto * unit = battle.battleGetUnitByID(entry.first);
			unit && spells::curePhysical::graveDustRemoval(*unit).empty())
		{
			affectedRecipients.insert(entry.first);
			auto line = MetaString::createFromRawString("Purifier removes Grave Dust from %s.");
			unit->addNameReplacement(line, unit->getCount());
			message.lines.push_back(std::move(line));
		}
	if(!message.lines.empty()) gameHandler.sendAndApply(message);
}
}
std::vector<uint32_t> applyPurifyAction(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const BattleAction & action, const CGHeroInstance * hero, const bool purifyingMandate)
{
	if(action.target.size() != 1 || action.target.front().unitValue != -1000)
		return {};
	std::set<uint32_t> affectedRecipients;

	const BattleHex center = action.target.front().hexValue;
	const bool purifier = newHorizonsPurify::hasPurifierPerk(hero);
	const auto eligible = newHorizonsPurify::eligibleStacks(battle, action.side, center,
		hero ? hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER) : 0, purifier);
	std::set<int32_t> eligibleUnitIds;
	for(const auto & stack : eligible)
		eligibleUnitIds.insert(stack.unitId);

	std::map<int32_t, std::vector<Bonus>> removalsByUnit;
	std::set<int32_t> magicallyCleansedUnits;
	std::set<int32_t> manuallySelectedPhysicalPoison;
	for(const auto & [unitId, sourceSpell] : action.spellPurifyChoices)
	{
		if(!eligibleUnitIds.contains(unitId))
			continue; // Recheck the shared center filter before any effect-group mutation.
		if(sourceSpell == newHorizonsPurify::physicalPoisonChoiceID())
		{
			manuallySelectedPhysicalPoison.insert(unitId);
			continue;
		}
		const auto * unit = battle.battleGetStackByID(unitId, false);
		if(!unit)
			continue;
		auto bonuses = newHorizonsPurify::spellEffectGroupBonuses(unit, sourceSpell);
		if(!bonuses.empty())
		{
			// Capture only genuinely magical removals before their source groups disappear.
			if(newHorizonsPurify::isMagicalSpellEffectGroup(unit, sourceSpell))
				magicallyCleansedUnits.insert(unitId);

			auto & selected = removalsByUnit[unitId];
			selected.insert(selected.end(), std::make_move_iterator(bonuses.begin()),
				std::make_move_iterator(bonuses.end()));
		}
	}

	const BattleID battleId = battle.getBattle()->getBattleID();
	if(!removalsByUnit.empty())
	{
		SetStackEffect removedEffects;
		removedEffects.battleID = battleId;
		for(auto & [unitId, bonuses] : removalsByUnit)
			removedEffects.toRemove.emplace_back(static_cast<ui32>(unitId), std::move(bonuses));
		gameHandler.sendAndApply(removedEffects);
		for(const auto & [unitId, sourceSpell] : action.spellPurifyChoices)
		{
			const auto * unit = battle.battleGetStackByID(unitId, false);
			if(sourceSpell != newHorizonsPurify::physicalPoisonChoiceID() && unit
				&& removalsByUnit.contains(unitId)
				&& newHorizonsPurify::spellEffectGroupBonuses(unit, sourceSpell).empty())
				affectedRecipients.insert(static_cast<uint32_t>(unitId));
		}

		BattleLogMessage message;
		message.battleID = battleId;
		for(const auto & removal : removalsByUnit)
		{
			const auto * unit = battle.battleGetStackByID(removal.first, false);
			if(!unit)
				continue;
			MetaString line;
			line.appendRawString("Purify removes negative spell effects from %s.");
			unit->addNameReplacement(line, unit->getCount());
			message.lines.push_back(std::move(line));
		}
		if(!message.lines.empty())
			gameHandler.sendAndApply(message);
	}

	clearStoredPoison(gameHandler, battle, eligible, manuallySelectedPhysicalPoison, affectedRecipients);
	clearGraveDust(gameHandler, battle, eligible, affectedRecipients);

	if(!purifyingMandate || magicallyCleansedUnits.empty())
		return {affectedRecipients.begin(), affectedRecipients.end()};

	SetStackEffect removePhysicalAfflictions;
	removePhysicalAfflictions.battleID = battleId;
	BattleUnitsChanged updateStoredPhysicalPoisons;
	updateStoredPhysicalPoisons.battleID = battleId;
	BattleLogMessage physicalAfflictionLog;
	physicalAfflictionLog.battleID = battleId;
	for(const int32_t unitId : magicallyCleansedUnits)
	{
		const auto * unit = battle.battleGetStackByID(unitId, false);
		if(!unit)
			continue;
		const auto affliction = physicalAfflictions::first(*unit);
		if(!affliction)
			continue;

		bool removed = false;
		if(affliction->storedPoison)
		{
			auto state = unit->acquireState();
			if(!newHorizonsPurify::clearPhysicalPoison(state.get()))
				continue;

			UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			updateStoredPhysicalPoisons.changedStacks.push_back(std::move(update));
			removed = true;
		}
		else
		{
			auto bonuses = physicalAfflictions::removalPlan(*unit, *affliction);
			if(bonuses.empty())
				continue;
			removePhysicalAfflictions.toRemove.emplace_back(static_cast<ui32>(unitId), std::move(bonuses));
			removed = true;
		}

		if(removed)
		{
			MetaString line;
			line.appendRawString("Purifying Mandate removes a physical affliction from %s.");
			unit->addNameReplacement(line, unit->getCount());
			physicalAfflictionLog.lines.push_back(std::move(line));
		}
	}

	if(!removePhysicalAfflictions.toRemove.empty())
		gameHandler.sendAndApply(removePhysicalAfflictions);
	if(!updateStoredPhysicalPoisons.changedStacks.empty())
		gameHandler.sendAndApply(updateStoredPhysicalPoisons);
	if(!physicalAfflictionLog.lines.empty())
		gameHandler.sendAndApply(physicalAfflictionLog);
	return {affectedRecipients.begin(), affectedRecipients.end()};
}
