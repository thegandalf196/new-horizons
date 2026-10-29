/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
class JsonNode;
class CHero;
class CGHeroInstance;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
DLL_LINKAGE void validateGhostSpecialtySerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE bool usesGhostUpgrade(const JsonNode & rules);
DLL_LINKAGE bool hasGhostUpgrade(const CGHeroInstance & hero);
DLL_LINKAGE bool resolveGhostSpecialtyPresentation(const ::CHero & hero, const JsonNode & rules,
	SpecialtyPresentation & result);
}
