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
struct LivingInfernoRules
{
	int damagePerCreature;
	int durationRounds;
};
DLL_LINKAGE void validateLivingInfernoSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE std::optional<LivingInfernoRules> livingInfernoRules(const JsonNode & rules);
DLL_LINKAGE bool hasLivingInferno(const CGHeroInstance * hero);
DLL_LINKAGE bool resolveLivingInfernoPresentation(const CHero & hero,
	const JsonNode & rules, SpecialtyPresentation & result);
}
