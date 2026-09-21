/*
 * NewHorizonsBattlecraft.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

class CGHeroInstance;

namespace newHorizonsBattlecraft
{
DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE int rankPercent(int rank);
DLL_LINKAGE bool hasEntrench(const CGHeroInstance * hero);
DLL_LINKAGE int defendReductionPercent(const CGHeroInstance * hero);
}
