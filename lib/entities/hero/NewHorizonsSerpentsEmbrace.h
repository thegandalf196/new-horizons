/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <optional>
class JsonNode;
class CGHeroInstance;
namespace newHorizonsHeroes
{
DLL_LINKAGE void validateSerpentsEmbraceSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE std::optional<bool> serpentsEmbracePolicy(const JsonNode & rules);
/// Presence selects the new specialty; false deliberately disables its effect.
DLL_LINKAGE bool hasSerpentsEmbrace(const CGHeroInstance * hero);
DLL_LINKAGE bool serpentsEmbraceEnabled(const CGHeroInstance * hero);
}
