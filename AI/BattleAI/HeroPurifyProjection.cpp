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
#include "HeroPurifyProjection.h"
#include "PossibleSpellcast.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsPurify.h"

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
	const bool selectionProjected = applySelection(selection, state, applyPurifyingMandate);

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
