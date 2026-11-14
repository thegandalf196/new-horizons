/*
 * NewHorizonsPerkRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#pragma once

#include "../../json/JsonNode.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nativeRules { class PreparedPilotPerks; }

namespace newHorizonsHeroes
{
/// The canonical registry is deliberately data-only.  The engine validates
/// its shape and identities, but does not assign meanings to individual
/// perks or effect payloads.
constexpr int PERK_MIN_RULESET_VERSION = 1;
constexpr int PERK_RULESET_VERSION = 2;
constexpr int PERK_SCHEMA_VERSION = 1;
constexpr int PERK_OFFER_WEIGHT_POLICY_VERSION = 2;
constexpr int PERK_PROGRESSION_POLICY_VERSION = 1;
constexpr double PERK_DEFAULT_MAXIMUM_PREFERENCE_RATIO = 2.0;
constexpr int PERK_OFFER_NORMALIZATION_MATH_VERSION = 1;
constexpr unsigned PERK_OFFER_WEIGHT_FRACTION_BITS = 32;
constexpr int PERK_MAX_SKILL_CHOICES = 2;
constexpr int PERK_MAX_PERK_CHOICES = 2;
/// The Version 1.0 contract allows at most three learned perks per Skill.
constexpr int PERK_MAX_PERKS_PER_SKILL = 3;
constexpr size_t PERK_LEGACY_SKILL_COUNT = 31;
constexpr size_t PERK_CANONICAL_SKILL_COUNT = 32;
constexpr size_t PERK_CANONICAL_POOL_SIZE = 10;
constexpr size_t PERK_BASIC_POOL_SIZE = 4;
constexpr size_t PERK_ADVANCED_POOL_SIZE = 4;
constexpr size_t PERK_EXPERT_POOL_SIZE = 2;

struct DLL_LINKAGE PerkDefinition
{
	std::string id;
	std::string name;
	std::string requiredRank;
	std::string description;
	JsonNode effect;
	std::string details;

	bool operator==(const PerkDefinition &) const = default;

	template<typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & name;
		h & requiredRank;
		h & description;
		h & effect;
		h & details;
	}
};

struct DLL_LINKAGE SkillDefinition
{
	std::string id;
	std::string name;
	std::string domain;
	std::string availability;
	std::string description;
	JsonNode ranks;
	std::vector<PerkDefinition> perks;
	std::string details;

	const std::string & rankDetails(std::string_view rank) const
	{
		static const std::string empty;
		const auto & value = ranks[std::string(rank)]["details"];
		return value.isString() ? value.String() : empty;
	}

	bool operator==(const SkillDefinition &) const = default;

	template<typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & name;
		h & domain;
		h & availability;
		h & description;
		h & ranks;
		h & perks;
		h & details;
	}
};

DLL_LINKAGE bool usesSylvanTactics(const JsonNode & rules);
DLL_LINKAGE void validateSylvanTacticsSerialization(const JsonNode & rules, bool supported);

DLL_LINKAGE bool usesPerkRules(const JsonNode & rules);
DLL_LINKAGE bool usesUnifiedSkillProgression(const JsonNode & rules);
DLL_LINKAGE void validateUnifiedSkillProgressionSerialization(const JsonNode & rules, bool supported);
/// Strict captured {version:1} marker; absence retains the historical 31-Skill roster.
DLL_LINKAGE bool usesCommandAuthorityPolicy(const JsonNode & rules);
DLL_LINKAGE size_t canonicalSkillCount(const JsonNode & rules);
DLL_LINKAGE void validateSkillOfferWeightRow(const JsonNode & classWeights, const JsonNode & rules);
/// Absence preserves the historical automatic fifth-level policy.
DLL_LINKAGE std::optional<int> quickStudyRerollInterval(const JsonNode & rules);

/// Validate the complete generic registry.  Empty/null rules retain legacy
/// semantics and are accepted; an active registry must match the canonical
/// schema's cardinalities and preserve every scoped identity.
DLL_LINKAGE void validatePerkRules(const JsonNode & rules);
DLL_LINKAGE std::shared_ptr<const nativeRules::PreparedPilotPerks> preparePilotPerkRules(const JsonNode & rules);
/// Absent markers preserve historical skills; present policies are strict captured objects.
DLL_LINKAGE void validatePassiveFactionPolicies(const JsonNode & rules);
/// Only the explicit captured marker opts into weighted perk selection.
/// Old registries remain legacy; a present malformed marker is rejected.
DLL_LINKAGE bool usesWeightedPerkOffers(const JsonNode & rules);
/// Parent-first normalized weighting; progression is independently captured.
DLL_LINKAGE bool usesR2PerkOffers(const JsonNode & rules);
/// Absence preserves historical progression, including policy2 same-tier choices.
DLL_LINKAGE bool usesStrictPerkProgression(const JsonNode & rules);
DLL_LINKAGE void validateWeightedPerkSerialization(const JsonNode & rules, bool supported, bool r2Supported = false,
	bool strictSupported = false, bool optionalRerollSupported = false, bool passiveFactionSupported = false);
/// Fresh-world paired admission only; historical resolved snapshots are not
/// retrofitted with a table or rejected for their intentionally absent marker.
DLL_LINKAGE void validateWeightedPerkCapture(const JsonNode & development, const JsonNode & perks);
DLL_LINKAGE bool crisisCommandProfileActive(const JsonNode & rules);
DLL_LINKAGE void validateCrisisCommandProfileSerialization(const JsonNode & rules, bool supported);

/// Parse one saved skill from a validated registry.  The returned value owns
/// copies of all data and therefore remains stable if the source is changed.
DLL_LINKAGE std::optional<SkillDefinition> perkSkill(const JsonNode & rules, std::string_view skillId);

/// Parse one saved perk from a validated registry.  Effects remain raw JSON;
/// no individual perk or effect literal is interpreted by this foundation.
DLL_LINKAGE std::optional<PerkDefinition> perkDefinition(const JsonNode & rules,
	std::string_view skillId, std::string_view perkId);

DLL_LINKAGE std::vector<PerkDefinition> perkOptions(const JsonNode & rules, std::string_view skillId);

/// Returns the minimum rank encoded by a generic registry prerequisite.
/// Unknown prerequisites are rejected by validatePerkRules().
DLL_LINKAGE int perkRequiredRank(std::string_view prerequisite);
}
