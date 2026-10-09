/*
 * NewHorizonsBattlecraft.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "BattleUnitTurnReason.h"

#include <cstdint>

class CGHeroInstance;

namespace battle
{
class CUnitState;
class Unit;
}

enum class BattlecraftMasteryAction : uint8_t
{
	WAIT,
	DEFEND,
};

namespace newHorizonsBattlecraft
{
DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int rankPercent(int rank);
DLL_LINKAGE bool hasEntrench(const CGHeroInstance * hero);
constexpr int OVERWATCH_DAMAGE_PERCENT = 50;
constexpr int OVERWATCH_RANGE = 10;
DLL_LINKAGE bool hasOverwatch(const CGHeroInstance * hero);
DLL_LINKAGE bool overwatchReady(const CGHeroInstance * hero, const battle::Unit * shooter, int32_t round);
DLL_LINKAGE int overwatchRange(const battle::Unit * shooter);
DLL_LINKAGE bool hasBattlefieldMastery(const CGHeroInstance * hero);
DLL_LINKAGE bool canAwardBattlefieldMastery(const CGHeroInstance * hero, const battle::Unit * stack,
	int32_t round, int32_t previousAwardRound, BattlecraftMasteryAction action);
DLL_LINKAGE int waitDamagePercent(const CGHeroInstance * hero, const battle::CUnitState * stack);
DLL_LINKAGE int defendReductionPercent(const CGHeroInstance * hero, const battle::CUnitState * stack);
DLL_LINKAGE bool hasPreemptiveStrike(const CGHeroInstance * hero);
DLL_LINKAGE int preemptiveStrikeDamagePercent(const CGHeroInstance * defenderHero,
	const battle::Unit * defender, int32_t round);
DLL_LINKAGE int defendReductionPercent(const CGHeroInstance * hero);
/// Speed granted only when an active Basic Reserve perk's ordinary creature
/// takes its delayed TURN_QUEUE activation after Waiting.
DLL_LINKAGE int delayedActivationMovementBonus(const CGHeroInstance * hero,
	const battle::CUnitState * stack, BattleUnitTurnReason reason);
}
