/*
 * SummonEvaluation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/spells/BattleSpellMechanics.h"

class CSpell;

namespace summonEvaluation
{
/// Canonical selected-empty-hex summons sharing the bounded board-wide shortlist.
bool usesSelectedPlacement(const spells::Mechanics * mechanics);
std::vector<spells::Target> placementTargets(const spells::Mechanics * mechanics);
/// Exact partial-stack value for newly created friendly summons skipped by the
/// ordinary health delta. Caller establishes creation and friendly ownership.
/// No extra lifetime weight: the existing continuation owns the bounded horizon.
float createdStackValue(const CSpell * spell, const battle::Unit * unit,
	bool convergenceElemental);
}
