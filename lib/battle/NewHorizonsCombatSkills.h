/*
 * NewHorizonsCombatSkills.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <string_view>

class CGHeroInstance;
namespace battle
{
class CUnitState;
class Unit;
}

namespace newHorizonsCombatSkills
{
constexpr std::string_view ARMORER_SKILL_ID = "new-horizons:armorer";
constexpr std::string_view COUNTERCHARGE_PERK_ID = "new-horizons:armorer.countercharge";
constexpr std::string_view FORMATION_FIGHTING_PERK_ID = "new-horizons:armorer.formationFighting";
constexpr std::string_view PAVISE_PERK_ID = "new-horizons:armorer.pavise";
constexpr std::string_view VETERAN_PERK_ID = "new-horizons:armorer.veteran";
constexpr int PAVISE_REDUCTION_PERCENT = 25;
constexpr int FORMATION_FIGHTING_REDUCTION_PERCENT = 10;
constexpr int VETERAN_RECOVERY_PERCENT = 15;

DLL_LINKAGE int armorerRank(const CGHeroInstance * hero);
DLL_LINKAGE int armorerReductionPercent(int rank);
/// Independent physical reduction while adjacent to a living friendly creature stack.
DLL_LINKAGE int formationFightingReductionPercent(const CGHeroInstance * hero);
/// Whether an attacker qualifies for ordinary creature-attack skill hit modifiers.
DLL_LINKAGE bool isOrdinaryCreatureAttacker(const battle::Unit * attacker);
/// Whether an attack can trigger creature Luck perks that require a physical creature attack.
DLL_LINKAGE bool isPhysicalCreatureLuckAttack(const battle::Unit * attacker, bool physicalDamage);
/// Independent ranged-physical reduction while the target is Defending.
DLL_LINKAGE int paviseReductionPercent(const CGHeroInstance * hero);
/// Consume the current physical-damage interval and apply Veteran's surviving-wound recovery.
DLL_LINKAGE std::int64_t applyVeteran(battle::CUnitState * state, const CGHeroInstance * hero);
DLL_LINKAGE int archeryRank(const CGHeroInstance * hero);
DLL_LINKAGE int archeryDamagePercent(int rank);
/// Apply Countercharge to Brace's Order-snapshot coefficient only. Other
/// pre-emptive damage, including Bulwark, is intentionally outside this rule.
DLL_LINKAGE int bracePreemptivePercent(int basePercent, bool hasCountercharge);
DLL_LINKAGE int bracePreemptivePercent(int basePercent, const CGHeroInstance * hero);
}
