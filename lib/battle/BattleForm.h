/*
 * BattleForm.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../bonuses/Bonus.h"

struct AccessibilityInfo;

namespace battle
{
class CUnitState;
class Unit;

DLL_LINKAGE Bonus polymorphMarker(SpellID spell, PlayerColor caster);
DLL_LINKAGE bool isPolymorphMarker(const Bonus * bonus);
/// Capture before round-timed bonuses age, so a final Spell Lock round still
/// protects the form's lifetime at this boundary.
DLL_LINKAGE bool battleFormDurationPaused(const Unit & unit);

/// Restore the original footprint by magical relocation. Accessibility must
/// release this unit's current footprint. If none fits, preserve form/HP/location
/// and mark restoration pending at lifetime one. Caller retains its marker.
DLL_LINKAGE bool endBattleFormAtNearestLegalPosition(CUnitState & state, const AccessibilityInfo & accessibility);
}
