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
DLL_LINKAGE void validateManaLeechesSerialization(const JsonNode & development, bool supported);
/// Historical absence never acquires the installed replacement policy.
DLL_LINKAGE std::optional<int> manaLeechesAmount(const JsonNode & development);
DLL_LINKAGE bool hasManaLeeches(const CGHeroInstance * hero);
}
