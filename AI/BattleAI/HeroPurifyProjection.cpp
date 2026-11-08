/*
 * HeroPurifyProjection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CurePhysicalAffliction.h"
#include "HeroPurifyProjection.h"
#include "PossibleSpellcast.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"

#include <limits>
#include <map>
#include <set>

namespace
{
/// Normalized selected payload, retaining per-unit source order and sorted unit traversal.
struct PurifySelectionProjection
{
	std::map<uint32_t, std::vector<SpellID>> groupsByUnit;
	std::set<uint32_t> physicalPoisonUnits;
	std::set<uint32_t> selectedUnits;
};

PurifySelectionProjection collectSelection(const PossibleSpellcast & ps)
{
	PurifySelectionProjection selection;
	for(const auto & [unitId, sourceSpell] : ps.spellPurifyChoices)
	{
		const auto id = static_cast<uint32_t>(unitId);
		selection.groupsByUnit[id].push_back(sourceSpell);
		selection.selectedUnits.insert(id);
	}
	for(const auto unitId : ps.spellPurifyPhysicalTargets)
	{
		const auto id = static_cast<uint32_t>(unitId);
		selection.physicalPoisonUnits.insert(id);
		selection.selectedUnits.insert(id);
	}
	return selection;
}

bool applySelection(const PurifySelectionProjection & selection, HypotheticBattle & state,
	bool applyPurifyingMandate)
{
	const auto & groupsByUnit = selection.groupsByUnit;
	const auto & physicalPoisonUnits = selection.physicalPoisonUnits;
	const auto & selectedUnits = selection.selectedUnits;
	bool selectionProjected = !selectedUnits.empty();
	for(const auto id : selectedUnits)
	{
		// Snapshot construction requires an existing unit. Check each ID here,
		// after any earlier selected unit was projected, preserving that prefix.
		if(!state.battleGetUnitByID(id))
		{
			selectionProjected = false;
			break;
		}
		auto projectedUnit = state.getForUpdate(id);
		if(!projectedUnit || !projectedUnit->alive())
		{
			selectionProjected = false;
			break;
		}
		const auto foundGroups = groupsByUnit.find(id);
		bool removesMagicalSpellGroup = false;
		if(foundGroups != groupsByUnit.end())
			for(const auto sourceSpell : foundGroups->second)
			{
				if(sourceSpell == newHorizonsPurify::physicalPoisonChoiceID())
				{
					if(!newHorizonsPurify::hasPhysicalPoison(projectedUnit.get()))
						selectionProjected = false;
					continue;
				}
				if(newHorizonsPurify::spellEffectGroupBonuses(projectedUnit.get(), sourceSpell).empty())
				{
					selectionProjected = false;
					continue;
				}
				removesMagicalSpellGroup = newHorizonsPurify::isMagicalSpellEffectGroup(
					projectedUnit.get(), sourceSpell) || removesMagicalSpellGroup;
			}
		if(physicalPoisonUnits.contains(id)
			&& !newHorizonsPurify::hasPhysicalPoison(projectedUnit.get()))
			selectionProjected = false;
		if(!selectionProjected)
			break;

		const auto groups = foundGroups == groupsByUnit.end()
			? std::vector<SpellID>{} : foundGroups->second;
		if(!projectedUnit->applyPurifySelection(groups, physicalPoisonUnits.contains(id),
			applyPurifyingMandate && removesMagicalSpellGroup))
		{
			selectionProjected = false;
			break;
		}
	}
	return selectionProjected;
}
std::set<uint32_t> applyAutomaticAfflictions(HypotheticBattle & state,
	const std::vector<newHorizonsPurify::EligibleStack> & eligible, const std::set<uint32_t> & manuallyPoisoned)
{
	std::set<uint32_t> resolved;
	for(const auto & row : eligible)
		if(row.physicalPoisonAutomaticallyCleared || manuallyPoisoned.contains(row.unitId))
			if(newHorizonsPurify::clearPhysicalPoison(state.getForUpdate(row.unitId).get())) resolved.insert(row.unitId);
	for(const auto & row : eligible)
	{
		const auto * unit = state.battleGetUnitByID(row.unitId);
		if(!unit) continue;
		if(row.graveDustAutomaticallyCleared)
		{
			const auto removed = spells::curePhysical::graveDustRemoval(*unit);
			if(!removed.empty()) { state.removeUnitBonus(row.unitId, removed); resolved.insert(row.unitId); }
		}
		if(row.stunAutomaticallyCleared)
		{
			const auto removed = newHorizonsStun::removalPlan(*state.battleGetUnitByID(row.unitId));
			if(!removed.empty()) { state.removeUnitBonus(row.unitId, removed); resolved.insert(row.unitId); }
		}
		if(row.crippleAutomaticallyCleared)
		{
			const auto removed = newHorizonsAttackControllers::crippleRemoval(*state.battleGetUnitByID(row.unitId));
			if(!removed.empty()) { state.removeUnitBonus(row.unitId, removed); resolved.insert(row.unitId); }
		}
	}
	return resolved;
}
}

void heroSpellProjection::projectPurifySelection(PossibleSpellcast & ps, HypotheticBattle & state,
	const CGHeroInstance * hero, BattleSide side, const float & baseline,
	const HypotheticBattle::ProjectedCounterspellOutcome & counterspell, bool counterspellNegated,
	const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t targetId)
{
	// Selection metadata is applied only to detached unit snapshots, never live callback state.
	const auto selection = collectSelection(ps);
	const bool applyPurifyingMandate = ps.divineMandateFollowup
		&& newHorizonsDivineMandate::hasPurifyingMandatePerk(hero);
	const auto eligible = ps.dest.size() == 1 && !ps.dest.front().unitValue
		? newHorizonsPurify::eligibleStacks(state, side, ps.dest.front().hexValue,
			hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), newHorizonsPurify::hasPurifierPerk(hero))
		: std::vector<newHorizonsPurify::EligibleStack>{};
	const bool selected = selection.selectedUnits.empty() || applySelection(selection, state, applyPurifyingMandate);
	const auto automatic = selected ? applyAutomaticAfflictions(state, eligible, selection.physicalPoisonUnits)
		: std::set<uint32_t>{};
	const bool selectionProjected = selected && (!selection.selectedUnits.empty() || !automatic.empty());

	// Purify deliberately accepts the projected action before testing negation.
	if(!selectionProjected
		|| !state.projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients)
		|| counterspellNegated)
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + ps.spellPurifyHeuristicValue;
}

std::optional<std::vector<uint32_t>> heroSpellProjection::projectAutomaticPurifyEffects(
	HypotheticBattle & state, const BattleAction & action, const CGHeroInstance * hero)
{
	if(!hero || action.target.size() != 1 || action.target.front().unitValue != -1000)
		return std::nullopt;
	const auto eligible = newHorizonsPurify::eligibleStacks(state, action.side,
		action.target.front().hexValue, hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER),
		newHorizonsPurify::hasPurifierPerk(hero));
	PossibleSpellcast selected;
	std::set<uint32_t> manuallyPoisoned, resolved;
	for(const auto & [id, source] : action.spellPurifyChoices)
		if(source == newHorizonsPurify::physicalPoisonChoiceID()) manuallyPoisoned.insert(id);
		else selected.spellPurifyChoices.emplace_back(id, source);
	const auto selection = collectSelection(selected);
	if(!selection.selectedUnits.empty())
	{
		if(!applySelection(selection, state, false)) return std::nullopt;
		resolved.insert(selection.selectedUnits.begin(), selection.selectedUnits.end());
	}
	// Shared auxiliary order follows the selected groups; automatic afflictions
	// remain outside the base selection cap and require Purifier eligibility.
	const auto automatic = applyAutomaticAfflictions(state, eligible, manuallyPoisoned);
	resolved.insert(automatic.begin(), automatic.end());
	return std::vector<uint32_t>{resolved.begin(), resolved.end()};
}
