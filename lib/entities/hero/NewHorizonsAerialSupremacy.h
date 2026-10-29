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
DLL_LINKAGE bool resolveAerialSupremacyPresentation(const CHero & hero, const JsonNode & rules,
	SpecialtyPresentation & result);
DLL_LINKAGE void validateAerialSupremacySerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE std::optional<bool> aerialSupremacyPolicy(const JsonNode & rules);
/// Presence selects the new specialty; false deliberately disables its effect.
DLL_LINKAGE bool hasAerialSupremacy(const CGHeroInstance * hero);
DLL_LINKAGE bool aerialSupremacyEnabled(const CGHeroInstance * hero);
}
