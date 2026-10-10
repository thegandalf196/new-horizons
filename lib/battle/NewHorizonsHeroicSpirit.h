/*
 * NewHorizonsHeroicSpirit.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../Global.h"
#include "BattleUnitTurnReason.h"

class CGHeroInstance;
namespace battle { class CUnitState; }

namespace newHorizonsHeroicSpirit
{
inline constexpr const char * SKILL = "new-horizons:discipline";
inline constexpr const char * PERK = "new-horizons:discipline.heroicSpirit";
DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero);
DLL_LINKAGE bool canGrantEarnedMorale(const battle::CUnitState & unit, const CGHeroInstance * hero);
/// Called only after the authoritative positive Morale roll succeeded.
DLL_LINKAGE bool grantEarnedMorale(battle::CUnitState & unit, const CGHeroInstance * hero);
/// Caller first establishes a genuine activation through battleBeginsActivation.
DLL_LINKAGE void beginActivation(battle::CUnitState & unit, BattleUnitTurnReason reason);
}
