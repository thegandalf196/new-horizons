/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
#include <vector>
struct Bonus;
class CGHeroInstance;
namespace newHorizonsHeroes
{
/// Cold attachment adapter; immutable Bonus templates use native live queries.
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> passiveCreatureSpecialtyBonuses(const CGHeroInstance & hero);
/// Adds captured rows the hero lacks, keeping present (possibly level-scaled) rows.
/// Windows 0.93.0 captured the rules but never installed these bonuses.
DLL_LINKAGE void restoreMissingPassiveCreatureSpecialtyBonuses(CGHeroInstance & hero);
}
