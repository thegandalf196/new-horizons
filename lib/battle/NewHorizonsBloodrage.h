/*
 * NewHorizonsBloodrage.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

class CGHeroInstance;

namespace newHorizonsBloodrage
{
constexpr int BASIC_INCREMENT = 5;
constexpr int ADVANCED_INCREMENT = 8;
constexpr int EXPERT_INCREMENT = 12;
constexpr int BASIC_CAP = 20;
constexpr int ADVANCED_CAP = 40;
constexpr int EXPERT_CAP = 60;

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int incrementForRank(int rank);
DLL_LINKAGE int capForRank(int rank);
DLL_LINKAGE bool hasWarDrums(const CGHeroInstance * hero);
DLL_LINKAGE int initialDamagePercent(const CGHeroInstance * hero);
DLL_LINKAGE int advanceDamagePercent(const CGHeroInstance * hero, int currentPercent);
}
