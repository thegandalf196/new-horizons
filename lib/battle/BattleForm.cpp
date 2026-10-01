/*
 * BattleForm.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleForm.h"
#include "AccessibilityInfo.h"
#include "CUnitState.h"
#include "../CCreatureHandler.h"

namespace battle
{
bool endBattleFormAtNearestLegalPosition(CUnitState & state, const AccessibilityInfo & accessibility)
{
	if(!state.hasBattleForm())
		return true;
	// Corpses do not reserve a footprint. Restore their health/provenance without
	// teleporting remains or competing with living units.
	if(!state.alive())
	{
		state.endBattleForm();
		return true;
	}
	const auto destination = accessibility.nearestLegalPosition(state.getPosition(),
		state.battleFormOriginalCreature().toCreature()->isDoubleWide(), state.unitSide());
	if(!destination)
		return false;
	state.endBattleForm();
	state.setPosition(*destination);
	return true;
}
}
