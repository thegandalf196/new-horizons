/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <optional>

class JsonNode;
class CHero;
class CGHeroInstance;
class CGTownInstance;
struct Bonus;

namespace newHorizonsHeroes
{
struct SpecialtyPresentation;

/// Captured hero specialties revised on 2026-11-20. Absence keeps every historical
/// specialty; each child row selects only its own hero's revised behaviour.
struct DLL_LINKAGE LordOfTheCryptsRules
{
	int skeletonGrowthPerLevel = 0;
};

DLL_LINKAGE void validateRevisedSpecialtiesSerialization(const JsonNode & development);
/// Captured content that requires NEW_HORIZONS_REVISED_SPECIALTIES (1166): revised hero
/// specialties, Estates policy 2 (core Stewardship) or Spellcraft policy 2 (Precise Casting).
DLL_LINKAGE bool requiresRevisedSpecialtyFormat(const JsonNode & document);
DLL_LINKAGE std::optional<LordOfTheCryptsRules> lordOfTheCryptsRules(const JsonNode & development);
/// Weekly growth Clavius adds to the Skeleton dwelling of the Necropolis town he stewards.
DLL_LINKAGE int lordOfTheCryptsGrowth(const CGHeroInstance & steward, const CGTownInstance & town, int level);
/// True when the hero's captured revised specialty replaces this prototype specialty bonus.
DLL_LINKAGE bool retiresSpecialtyBonus(const CGHeroInstance & hero, const Bonus & bonus);
/// True when this hero's revised specialty gains value from being a steward.
DLL_LINKAGE bool benefitsFromStewardship(const CGHeroInstance & hero, const CGTownInstance & town);
DLL_LINKAGE bool resolveRevisedSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
