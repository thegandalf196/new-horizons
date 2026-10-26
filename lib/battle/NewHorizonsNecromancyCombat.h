/*
 * NewHorizonsNecromancyCombat.h, part of VCMI engine
 * Authors: listed in file AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../constants/EntityIdentifiers.h"

class CGHeroInstance;
namespace battle { class Unit; }
struct BattleAttackInfo;
struct DamageEstimation;

namespace newHorizonsNecromancy
{
/// Captured per-controller inputs; recipient form and current allegiance remain query-time facts.
struct CombatPassives
{
	int movementSpeed = 0;
	int magicalReductionPercent = 0;
	int additionalRetaliations = 0;
};
DLL_LINKAGE CombatPassives captureCombatPassives(const CGHeroInstance * hero);
DLL_LINKAGE CombatPassives passivesForCreature(const CombatPassives & captured, CreatureID creature);
/// Construction-only query: old captures and zero-passive recipients retain lazy projection.
DLL_LINKAGE bool needsCombatPassiveSnapshot(const battle::Unit & unit);
/// Paths with no historical general MDR stage acquire only the new independent source.
DLL_LINKAGE int64_t adjustUnmitigatedMagicalDamage(const battle::Unit & target, int64_t rawDamage);
/// Existing magical creature attacks have no general MDR stage: add only this new source.
DLL_LINKAGE void adjustCreatureDamage(const BattleAttackInfo & attack, DamageEstimation & damage);
}
