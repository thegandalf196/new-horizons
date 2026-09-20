/*
 * NewHorizonsHeroRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NewHorizonsPrimaryProfile.h"
#include "NewHorizonsPrimaryGrowth.h"
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace newHorizonsHeroes
{
constexpr int HERO_RULESET_VERSION = 1;
/// The canonical New Horizons offer table covers the 31 skills in Tables
/// 45-47 of New Horizons.docx.  Keep this count beside the runtime validator
/// so an incomplete table cannot silently fall back to legacy chances.
constexpr size_t HERO_SKILL_OFFER_COUNT = 31;

struct DLL_LINKAGE SkillGrowthChance
{
	SecondarySkill skill;
	PrimarySkill attribute;
	int chancePercent = 0;
};

/// Read-only live-hero presentation. No secondary attributes or masteries are
/// fabricated here. Guaranteed growth excludes the independent chance outcomes.
struct DLL_LINKAGE PrimaryGrowthView
{
	PrimaryProfile profile;
	std::array<int, GameConstants::PRIMARY_SKILLS> base{};
	std::array<int, GameConstants::PRIMARY_SKILLS> modified{};
	std::vector<SkillGrowthChance> extraGrowth;
	std::array<int, GameConstants::PRIMARY_SKILLS> lastGains{};
	int powerDivisor = 1;
	int maximumPrimary = 0;
};

DLL_LINKAGE bool usesRules(const JsonNode & rules);
/// Whether a resolved hero snapshot carries the canonical per-class offer map.
/// Old snapshots intentionally return false and retain legacy behaviour.
DLL_LINKAGE bool usesSkillOfferWeights(const JsonNode & resolvedRules);
/// Returns the configured offer weight.  A present zero is meaningful (the
/// skill is excluded); nullopt means that this is an old or incomplete
/// snapshot and callers should use their legacy path.
DLL_LINKAGE std::optional<int> skillOfferWeight(const JsonNode & resolvedRules, SecondarySkill skill);
/// Returns whether a skill is explicitly retired from ordinary level-up
/// offers (for example Mysticism, which is now a perk).
DLL_LINKAGE bool isExcludedSkill(const JsonNode & resolvedRules, SecondarySkill skill);
/// New-game completeness differs from validation of an existing saved roster.
DLL_LINKAGE void validateHeroRules(const JsonNode & rules, bool requireAllClasses);
DLL_LINKAGE void validateResolvedHeroRules(const JsonNode & rules);
DLL_LINKAGE JsonNode resolveHeroRules(const JsonNode & rules, HeroClassID heroClass);
DLL_LINKAGE std::vector<SkillGrowthChance> skillGrowthChances(const JsonNode & resolvedRules,
	const std::function<int(SecondarySkill)> & rank);

/// Returns the canonical New Horizons faction skill for a faction in a
/// resolved hero-rules snapshot. An empty result means that the snapshot does
/// not carry faction-skill rules (for example, an old saved hero).
DLL_LINKAGE std::optional<SecondarySkill> factionSkill(const JsonNode & resolvedRules, FactionID faction);

/// Converts the legacy skills authored in a hero type/map into the canonical
/// New Horizons skill identities for a newly-created hero. Perk-only legacy
/// skills are intentionally removed until a real perk-at-start path exists;
/// this function never invents the parent skill. Duplicate targets retain the
/// first position and the highest source rank. The input is never modified.
DLL_LINKAGE std::vector<std::pair<SecondarySkill, ui8>> migrateStartingSkills(
	const JsonNode & resolvedRules, FactionID faction,
	const std::vector<std::pair<SecondarySkill, ui8>> & initialSkills);

/// Returns whether a skill is one of the faction-unique skills in a resolved
/// snapshot. Legacy aliases are included so they cannot become foreign
/// faction choices when a saved roster still carries the old identity.
DLL_LINKAGE bool isFactionSkill(const JsonNode & resolvedRules, SecondarySkill skill);

/// Returns whether a skill belongs to the requested faction, accepting its
/// configured legacy alias as the same identity.
DLL_LINKAGE bool isFactionSkillForFaction(const JsonNode & resolvedRules, FactionID faction, SecondarySkill skill);

/// Applies the faction skill promised by the active New Horizons rules to a
/// newly-created hero's default skill list. Existing saved heroes are not
/// passed here: their serialized skill identities remain authoritative.
DLL_LINKAGE std::vector<std::pair<SecondarySkill, ui8>> applyStartingFactionSkill(
	const JsonNode & resolvedRules, bool magicHero, FactionID faction,
	const std::vector<std::pair<SecondarySkill, ui8>> & initialSkills);
}
