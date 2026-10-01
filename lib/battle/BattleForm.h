/*
 * BattleForm.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

struct AccessibilityInfo;

namespace battle
{
class CUnitState;

/// Restore the original footprint by magical relocation. Accessibility must
/// release this unit's current footprint. If none fits, leave the state unchanged.
DLL_LINKAGE bool endBattleFormAtNearestLegalPosition(CUnitState & state, const AccessibilityInfo & accessibility);
}
