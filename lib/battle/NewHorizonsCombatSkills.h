/*
 * NewHorizonsCombatSkills.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <string_view>

class CGHeroInstance;
namespace battle
{
class Unit;
}

namespace newHorizonsCombatSkills
{
constexpr std::string_view ARMORER_SKILL_ID = "new-horizons:armorer";
constexpr std::string_view COUNTERCHARGE_PERK_ID = "new-horizons:armorer.countercharge";
constexpr std::string_view PAVISE_PERK_ID = "new-horizons:armorer.pavise";
constexpr int PAVISE_REDUCTION_PERCENT = 25;

DLL_LINKAGE int armorerRank(const CGHeroInstance * hero);
DLL_LINKAGE int armorerReductionPercent(int rank);
/// Whether an attacker qualifies for ordinary creature-attack skill hit modifiers.
DLL_LINKAGE bool isOrdinaryCreatureAttacker(const battle::Unit * attacker);
/// Independent ranged-physical reduction while the target is Defending.
DLL_LINKAGE int paviseReductionPercent(const CGHeroInstance * hero);
DLL_LINKAGE int archeryRank(const CGHeroInstance * hero);
DLL_LINKAGE int archeryDamagePercent(int rank);
/// Apply Countercharge to Brace's Order-snapshot coefficient only. Other
/// pre-emptive damage, including Bulwark, is intentionally outside this rule.
DLL_LINKAGE int bracePreemptivePercent(int basePercent, bool hasCountercharge);
DLL_LINKAGE int bracePreemptivePercent(int basePercent, const CGHeroInstance * hero);
}
