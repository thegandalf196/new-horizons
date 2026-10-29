/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
class JsonNode;
class CGHeroInstance;
class CHero;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
DLL_LINKAGE void validatePetrifyingBloodSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE bool hasPetrifyingBlood(const CGHeroInstance * hero);
DLL_LINKAGE bool resolvePetrifyingBloodPresentation(const CHero & hero, const JsonNode & rules,
	SpecialtyPresentation & result);
}
