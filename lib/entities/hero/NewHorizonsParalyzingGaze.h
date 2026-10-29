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
DLL_LINKAGE bool resolveParalyzingGazePresentation(const CHero & hero, const JsonNode & development,
	SpecialtyPresentation & result);
DLL_LINKAGE void validateParalyzingGazeSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE std::optional<bool> paralyzingGazePolicy(const JsonNode & rules);
/// Presence selects the new specialty; false deliberately disables its effect.
DLL_LINKAGE bool hasParalyzingGaze(const CGHeroInstance * hero);
DLL_LINKAGE bool paralyzingGazeEnabled(const CGHeroInstance * hero);
}
