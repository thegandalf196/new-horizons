/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../ResourceSet.h"

class CGHeroInstance;
namespace newHorizonsHeroes
{
struct CutpurseRules;
struct SulfurSenseRules;
DLL_LINKAGE TResource cutpurseGold(const CutpurseRules & rules, int level,
	TResource treasury, TResource recipientGold);
DLL_LINKAGE int sulfurSenseAmount(const SulfurSenseRules & rules, int level);
DLL_LINKAGE TResource cutpurseGold(const CGHeroInstance * hero, TResource treasury, TResource recipientGold);
DLL_LINKAGE int sulfurSenseAmount(const CGHeroInstance * hero);
DLL_LINKAGE ResourceSet sulfurSensePickup(const ResourceSet & reward, const CGHeroInstance * hero);
}
