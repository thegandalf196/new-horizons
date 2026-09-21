/*
 * NewHorizonsShroud.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <string_view>

class CGHeroInstance;

namespace newHorizonsShroud
{
constexpr std::string_view SKILL_ID = "new-horizons:shroudOfMalassa";

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int flankingDamagePercent(int rank);
DLL_LINKAGE bool deniesRetaliation(int rank);
}
