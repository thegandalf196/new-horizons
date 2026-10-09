/*
 * NewHorizonsCombatSkills.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <string_view>

class CGHeroInstance;
struct Bonus;
namespace battle
{
class CUnitState;
class Unit;
}

namespace newHorizonsCombatSkills
{
constexpr std::string_view LUCK_SKILL_ID = "new-horizons:luck";
constexpr std::string_view GAMBLER_PERK_ID = "new-horizons:luck.gambler";
constexpr std::string_view LUCKY_RECOVERY_PERK_ID = "new-horizons:luck.luckyRecovery";
DLL_LINKAGE bool hasLuckyRecovery(const CGHeroInstance * hero);
/// Independent generic/Sylvan 10% contributions, floored once after summing.
DLL_LINKAGE int64_t luckyRecoveryAmount(int64_t actualDamage, bool genericRecovery, bool sylvanRecovery);
constexpr int GAMBLER_LUCK_PENALTY = -2;
DLL_LINKAGE Bonus gamblerLuckPenalty();
DLL_LINKAGE bool isGamblerLuckPenalty(const Bonus * bonus);
constexpr std::string_view ARMORER_SKILL_ID = "new-horizons:armorer";
constexpr std::string_view COUNTERCHARGE_PERK_ID = "new-horizons:armorer.countercharge";
constexpr std::string_view FORMATION_FIGHTING_PERK_ID = "new-horizons:armorer.formationFighting";
constexpr std::string_view PAVISE_PERK_ID = "new-horizons:armorer.pavise";
constexpr std::string_view VETERAN_PERK_ID = "new-horizons:armorer.veteran";
constexpr std::string_view BASTION_PERK_ID = "new-horizons:armorer.bastion";
constexpr int PAVISE_REDUCTION_PERCENT = 25;
constexpr int BASTION_FINAL_DAMAGE_MULTIPLIER = 70;
constexpr int FORMATION_FIGHTING_REDUCTION_PERCENT = 10;
constexpr int VETERAN_RECOVERY_PERCENT = 15;

DLL_LINKAGE int armorerRank(const CGHeroInstance * hero);
DLL_LINKAGE int armorerReductionPercent(int rank);
/// Returns Armorer's core reduction after the saved Skill specialty, if present.
DLL_LINKAGE int armorerReductionPercent(const CGHeroInstance * hero);
/// Independent physical reduction while adjacent to a living friendly creature stack.
DLL_LINKAGE int formationFightingReductionPercent(const CGHeroInstance * hero);
/// Whether an attacker qualifies for ordinary creature-attack skill hit modifiers.
DLL_LINKAGE bool isOrdinaryCreatureAttacker(const battle::Unit * attacker);
/// Whether an attack is physical damage from an ordinary creature stack (not a war machine).
DLL_LINKAGE bool isPhysicalCreatureAttack(const battle::Unit * attacker, bool physicalDamage);
/// Whether an attack can trigger creature Luck perks that require a physical creature attack.
DLL_LINKAGE bool isPhysicalCreatureLuckAttack(const battle::Unit * attacker, bool physicalDamage);
/// Independent ranged-physical reduction while the target is Defending.
DLL_LINKAGE int paviseReductionPercent(const CGHeroInstance * hero);
/// Consume the current physical-damage interval and apply Veteran's surviving-wound recovery.
DLL_LINKAGE std::int64_t applyVeteran(battle::CUnitState * state, const CGHeroInstance * hero);
DLL_LINKAGE int archeryRank(const CGHeroInstance * hero);
DLL_LINKAGE int archeryDamagePercent(int rank);
/// Returns Archery's core ranged-damage contribution after its saved Skill specialty, if present.
DLL_LINKAGE int archeryDamagePercent(const CGHeroInstance * hero);
/// Apply Countercharge to Brace's Order-snapshot coefficient only. Other
/// pre-emptive damage, including Bulwark, is intentionally outside this rule.
DLL_LINKAGE int bracePreemptivePercent(int basePercent, bool hasCountercharge);
DLL_LINKAGE int bracePreemptivePercent(int basePercent, const CGHeroInstance * hero);
}
