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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace newHorizonsHeroes
{
/// The canonical registry is deliberately data-only.  The engine validates
/// its shape and identities, but does not assign meanings to individual
/// perks or effect payloads.
constexpr int PERK_RULESET_VERSION = 1;
constexpr int PERK_SCHEMA_VERSION = 1;
constexpr int PERK_MAX_SKILL_CHOICES = 2;
constexpr int PERK_MAX_PERK_CHOICES = 2;
/// The Version 1.0 contract allows at most three learned perks per Skill.
constexpr int PERK_MAX_PERKS_PER_SKILL = 3;
constexpr size_t PERK_CANONICAL_SKILL_COUNT = 31;
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

	bool operator==(const PerkDefinition &) const = default;

	template<typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & name;
		h & requiredRank;
		h & description;
		h & effect;
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
	}
};

DLL_LINKAGE bool usesPerkRules(const JsonNode & rules);

/// Validate the complete generic registry.  Empty/null rules retain legacy
/// semantics and are accepted; an active registry must match the canonical
/// schema's cardinalities and preserve every scoped identity.
DLL_LINKAGE void validatePerkRules(const JsonNode & rules);

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
