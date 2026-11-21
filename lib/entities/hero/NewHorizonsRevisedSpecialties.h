/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>

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

/// The all-faction level rule for flat specialty values: the authored value is the
/// level 1 value and grows by this percentage of itself per further level.
struct DLL_LINKAGE SpecialtyLevelScaling
{
	int percentOfBasePerLevel = 0;
};

DLL_LINKAGE void validateRevisedSpecialtiesSerialization(const JsonNode & development);
/// Captured content that requires NEW_HORIZONS_REVISED_SPECIALTIES (1166): revised hero
/// specialties, Estates policy 2 (core Stewardship) or Spellcraft policy 2 (Precise Casting).
DLL_LINKAGE bool requiresRevisedSpecialtyFormat(const JsonNode & document);
DLL_LINKAGE std::optional<SpecialtyLevelScaling> specialtyLevelScaling(const JsonNode & development);
/// Level-scaled value of one captured ordinary specialty coefficient; unlisted
/// coefficients and historical captures return the authored value unchanged.
DLL_LINKAGE int levelScaledOrdinaryValue(const CGHeroInstance & hero, std::string_view coefficient, int authored);
/// Same rule for an explicit level, used by presentation and tests.
DLL_LINKAGE int levelScaledOrdinaryValue(const SpecialtyLevelScaling & scaling, std::string_view coefficient,
	int authored, int level);
DLL_LINKAGE bool scalesWithLevel(std::string_view coefficient);
/// After a level-up, replace this hero's level-scaled ordinary specialty bonus.
/// Historical captures without the level rule are left untouched.
DLL_LINKAGE void refreshLevelScaledSpecialtyBonus(CGHeroInstance & hero);
/// Tamika's Dread Cavalry follow-up damage on the +3%/level rule, at most 100%.
DLL_LINKAGE int levelScaledDreadCavalryPercent(const CGHeroInstance & hero, int authored);
/// Ignatius's Mana Leeches: +1 Mana stolen per hit for every 10 hero levels.
DLL_LINKAGE int levelScaledManaLeeches(const CGHeroInstance & hero, int authored);
/// Player text for the level rule of one coefficient, or empty when it does not scale.
DLL_LINKAGE std::string levelScalingDescription(const SpecialtyLevelScaling & scaling, std::string_view coefficient);
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
