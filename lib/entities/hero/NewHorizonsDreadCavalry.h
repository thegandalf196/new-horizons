/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <optional>
class JsonNode;
class CGHeroInstance;
class CHero;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
DLL_LINKAGE void validateDreadCavalrySerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE std::optional<int> dreadCavalryDamagePercent(const JsonNode & rules);
DLL_LINKAGE bool hasDreadCavalry(const CGHeroInstance * hero);
DLL_LINKAGE bool resolveDreadCavalryPresentation(const CHero & hero,
	const JsonNode & rules, SpecialtyPresentation & result);
}
