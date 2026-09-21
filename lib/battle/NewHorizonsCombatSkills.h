/*
 * NewHorizonsCombatSkills.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

class CGHeroInstance;

namespace newHorizonsCombatSkills
{
DLL_LINKAGE int armorerRank(const CGHeroInstance * hero);
DLL_LINKAGE int armorerReductionPercent(int rank);
DLL_LINKAGE int archeryRank(const CGHeroInstance * hero);
DLL_LINKAGE int archeryDamagePercent(int rank);
}
