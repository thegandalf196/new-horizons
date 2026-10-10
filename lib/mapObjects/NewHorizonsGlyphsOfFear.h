/*
 * NewHorizonsGlyphsOfFear.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../bonuses/BonusList.h"
#include "../int3.h"
#include <optional>
class CGHeroInstance;
namespace newHorizonsGlyphsOfFear
{
/// Query-only environmental components, never inserted into the bonus graph.
/// Adventure queries use the hero's current tile; combat supplies its saved location.
DLL_LINKAGE TConstBonusListPtr moraleBonuses(const CGHeroInstance * hero,
	std::optional<int3> location = std::nullopt);
DLL_LINKAGE TConstBonusListPtr appendMoraleBonuses(TConstBonusListPtr original,
	const CGHeroInstance * hero, std::optional<int3> location = std::nullopt);
}
