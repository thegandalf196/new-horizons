/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
#include <vector>
class Bonus;
class CGHeroInstance;
namespace newHorizonsHeroes
{
/// Cold attachment adapter; immutable Bonus templates use native live queries.
DLL_LINKAGE std::vector<std::shared_ptr<Bonus>> passiveCreatureSpecialtyBonuses(const CGHeroInstance & hero);
}
