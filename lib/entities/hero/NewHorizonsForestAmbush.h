/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../../Global.h"
#include <optional>
class JsonNode;
class CGHeroInstance;
class CHero;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
DLL_LINKAGE bool resolveForestAmbushPresentation(const CHero & hero, const JsonNode & rules, SpecialtyPresentation & result);
DLL_LINKAGE void validateForestAmbushSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE std::optional<bool> forestAmbushPolicy(const JsonNode & rules);
/// Presence selects the new specialty; false deliberately disables its effect.
DLL_LINKAGE bool hasForestAmbush(const CGHeroInstance * hero);
DLL_LINKAGE bool forestAmbushEnabled(const CGHeroInstance * hero);
}
