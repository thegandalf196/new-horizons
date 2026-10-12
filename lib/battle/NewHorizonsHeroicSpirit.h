/*
 * NewHorizonsHeroicSpirit.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../Global.h"
#include "BattleUnitTurnReason.h"
#include "HeroActionAllowanceState.h"

class CGHeroInstance;
namespace battle { class CUnitState; }

namespace newHorizonsHeroicSpirit
{
inline constexpr const char * SKILL = "new-horizons:discipline";
inline constexpr const char * PERK = "new-horizons:discipline.heroicSpirit";
DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero);
/// Read-only preflight; the server resolves the configured positive Hero Morale
/// draw only for an accepted flexible-Hero Order, never for bonus Orders.
DLL_LINKAGE bool canRollOrderMorale(const CGHeroInstance * hero, const HeroActionAllowanceState::Receipt & receipt);
/// Retired creature mechanic. Kept inert for legacy event/AI call sites while
/// its serialized fields remain readable; Heroic Spirit now grants Orders.
DLL_LINKAGE bool canGrantEarnedMorale(const battle::CUnitState & unit, const CGHeroInstance * hero);
/// Retired compatibility entry point; never produces creature state.
DLL_LINKAGE bool grantEarnedMorale(battle::CUnitState & unit, const CGHeroInstance * hero);
/// Clear any retired fields at a genuine activation; does not award an action.
DLL_LINKAGE void beginActivation(battle::CUnitState & unit, BattleUnitTurnReason reason);
}
