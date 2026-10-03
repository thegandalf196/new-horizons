/*
 * NewHorizonsShroud.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <string_view>

class CGHeroInstance;
struct Bonus;

namespace newHorizonsShroud
{
constexpr std::string_view SKILL_ID = "new-horizons:shroudOfMalassa";
constexpr std::string_view BACKSTAB_PERK_ID = "new-horizons:shroudOfMalassa.backstab";
constexpr std::string_view NO_ESCAPE_PERK_ID = "new-horizons:shroudOfMalassa.noEscape";
constexpr std::string_view NO_ESCAPE_STACKING_KEY = "new-horizons:shroudOfMalassa.noEscape";
constexpr int BACKSTAB_DAMAGE_PERCENT = 15;
constexpr int NO_ESCAPE_SPEED_PENALTY = -2;

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int flankingDamagePercent(int rank);
DLL_LINKAGE int backstabDamagePercent(const CGHeroInstance * hero);
DLL_LINKAGE bool hasNoEscape(const CGHeroInstance * hero);
DLL_LINKAGE Bonus noEscapeSpeedPenalty();
DLL_LINKAGE bool isNoEscapeSpeedPenalty(const Bonus * bonus);
DLL_LINKAGE bool deniesRetaliation(int rank);
}
