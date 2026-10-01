/*
 * NewHorizonsBattlecraft.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "BattleUnitTurnReason.h"

class CGHeroInstance;

namespace battle
{
class CUnitState;
}

namespace newHorizonsBattlecraft
{
DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int rankPercent(int rank);
DLL_LINKAGE bool hasEntrench(const CGHeroInstance * hero);
DLL_LINKAGE int defendReductionPercent(const CGHeroInstance * hero);
/// Speed granted only when an active Basic Reserve perk's ordinary creature
/// takes its delayed TURN_QUEUE activation after Waiting.
DLL_LINKAGE int delayedActivationMovementBonus(const CGHeroInstance * hero,
	const battle::CUnitState * stack, BattleUnitTurnReason reason);
}
