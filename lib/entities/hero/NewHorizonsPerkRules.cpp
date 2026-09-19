/*
 * NewHorizonsPerkRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsPerkRules.h"

#include <array>
#include <cmath>
#include <cctype>
#include <set>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
void require(bool valid, const std::string & detail)
{
	if(!valid)
		throw std::runtime_error("Invalid New Horizons perk rules: " + detail);
}

void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

bool integer(const JsonNode & node, int minimum, int maximum)
{
	return node.isNumber() && std::isfinite(node.Float()) && node.Float() >= minimum && node.Float() <= maximum
		&& std::floor(node.Float()) == node.Float();
}

bool scopedId(std::string_view id)
{
	const auto separator = id.find(':');
	if(separator == std::string_view::npos || separator == 0 || separator + 1 >= id.size()
		|| id.find(':', separator + 1) != std::string_view::npos)
		return false;
	for(size_t i = 0; i < id.size(); ++i)
	{
		if(i == separator)
			continue;
		const auto c = static_cast<unsigned char>(id[i]);
		if(i == 0 || (i == separator + 1))
		{
			if(!std::islower(c))
				return false;
		}
		else if(i < separator)
		{
			if(!(std::islower(c) || std::isdigit(c) || c == '_' || c == '-' || c == '.'))
				return false;
		}
		else if(!(std::isalnum(c) || c == '_' || c == '-' || c == '.'))
			return false;
	}
	return true;
}

void validateEffect(const JsonNode & effect, const std::string & context)
{
	fields(effect, {"status", "description"});
	require(effect["status"].isString() && (effect["status"].String() == "active"
		|| effect["status"].String() == "planned"), context + " effect status");
	require(effect["description"].isString() && !effect["description"].String().empty(), context + " effect description");
}

void validateRank(const JsonNode & rank, const std::string & context)
{
	fields(rank, {"description", "effect"});
	require(rank["description"].isString() && !rank["description"].String().empty(), context + " description");
	validateEffect(rank["effect"], context);
}

void validatePerk(const JsonNode & node, const std::string & skillId, const std::string & context,
	std::set<std::string> & identities)
{
	fields(node, {"id", "name", "requires", "description", "effect"});
	for(const auto * key : {"id", "name", "requires", "description"})
		require(node[key].isString() && !node[key].String().empty(), context + " nonempty " + key);
	require(scopedId(node["id"].String()), context + " scoped id");
	require(node["id"].String().starts_with(skillId + "."), context + " must belong to its skill");
	require(identities.insert(node["id"].String()).second, context + " duplicate id");
	require(node["requires"].String() == "basic" || node["requires"].String() == "advanced"
		|| node["requires"].String() == "expert", context + " prerequisite");
	validateEffect(node["effect"], context);
}

void parsePerk(const JsonNode & node, PerkDefinition & result)
{
	result.id = node["id"].String();
	result.name = node["name"].String();
	result.requiredRank = node["requires"].String();
	result.description = node["description"].String();
	result.effect = node["effect"];
}

void parseSkill(const JsonNode & node, SkillDefinition & result)
{
	result.id = node["id"].String();
	result.name = node["name"].String();
	result.domain = node["domain"].String();
	result.availability = node["availability"].String();
	result.description = node["description"].String();
	result.ranks = node["ranks"];
	result.perks.clear();
	for(const auto & entry : node["perks"].Vector())
	{
		PerkDefinition perk;
		parsePerk(entry, perk);
		result.perks.push_back(std::move(perk));
	}
}
}

bool usesPerkRules(const JsonNode & rules)
{
	return !rules.isNull() && !(rules.isStruct() && rules.Struct().empty());
}

int perkRequiredRank(std::string_view prerequisite)
{
	if(prerequisite == "basic")
		return 1;
	if(prerequisite == "advanced")
		return 2;
	if(prerequisite == "expert")
		return 3;
	throw std::runtime_error("Unknown New Horizons perk prerequisite");
}

void validatePerkRules(const JsonNode & rules)
{
	if(!usesPerkRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "sourceDocument", "sourceSha256",
		"maxSkillChoices", "maxPerkChoices", "maxPerksPerSkill", "skills"});
	require(integer(rules["schemaVersion"], PERK_SCHEMA_VERSION, PERK_SCHEMA_VERSION), "schemaVersion");
	require(integer(rules["rulesetVersion"], PERK_RULESET_VERSION, PERK_RULESET_VERSION), "rulesetVersion");
	require(rules["sourceDocument"].isString() && !rules["sourceDocument"].String().empty(), "sourceDocument");
	require(rules["sourceSha256"].isString() && rules["sourceSha256"].String().size() == 64, "sourceSha256");
	for(const auto c : rules["sourceSha256"].String())
		require(std::isxdigit(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(c)), "sourceSha256");
	require(integer(rules["maxSkillChoices"], PERK_MAX_SKILL_CHOICES, PERK_MAX_SKILL_CHOICES), "maxSkillChoices");
	require(integer(rules["maxPerkChoices"], PERK_MAX_PERK_CHOICES, PERK_MAX_PERK_CHOICES), "maxPerkChoices");
	require(integer(rules["maxPerksPerSkill"], PERK_MAX_PERKS_PER_SKILL, PERK_MAX_PERKS_PER_SKILL),
		"maxPerksPerSkill");
	require(rules["skills"].isStruct() && rules["skills"].Struct().size() == PERK_CANONICAL_SKILL_COUNT,
		"complete skill coverage required");
	std::set<std::string> skills;
	std::set<std::string> identities;
	for(const auto & [key, node] : rules["skills"].Struct())
	{
		fields(node, {"id", "name", "domain", "availability", "description", "ranks", "perks"});
		require(scopedId(key), "scoped skill key");
		require(node["id"].isString() && node["id"].String() == key, "skill key/id mismatch");
		require(skills.insert(key).second, "duplicate skill id");
		require(node["name"].isString() && !node["name"].String().empty(), "skill name");
		static const std::set<std::string> domains = {"Martial", "Magic", "Hybrid", "Strategic", "Faction"};
		static const std::set<std::string> availability = {"All heroes", "Might-exclusive", "Magic-exclusive",
			"Castle heroes only", "Rampart heroes only", "Tower heroes only", "Dungeon heroes only",
			"Inferno heroes only", "Necropolis heroes only", "Stronghold heroes only",
			"Fortress heroes only", "Conflux heroes only"};
		require(node["domain"].isString() && domains.contains(node["domain"].String()), "skill domain");
		require(node["availability"].isString() && availability.contains(node["availability"].String()),
			"skill availability");
		require(node["description"].isString() && !node["description"].String().empty(), "skill description");
		fields(node["ranks"], {"basic", "advanced", "expert"});
		for(const auto * rank : {"basic", "advanced", "expert"})
			validateRank(node["ranks"][rank], key + " rank " + rank);
		require(node["perks"].isVector() && node["perks"].Vector().size() == PERK_CANONICAL_POOL_SIZE,
			key + " must have ten perks");
		std::array<size_t, 3> counts{};
		for(size_t i = 0; i < node["perks"].Vector().size(); ++i)
		{
			validatePerk(node["perks"].Vector()[i], key, key + " perk " + std::to_string(i), identities);
			const auto & prerequisite = node["perks"].Vector()[i]["requires"].String();
			if(prerequisite == "basic")
				++counts[0];
			else if(prerequisite == "advanced")
				++counts[1];
			else
				++counts[2];
		}
		require(counts[0] == PERK_BASIC_POOL_SIZE && counts[1] == PERK_ADVANCED_POOL_SIZE
			&& counts[2] == PERK_EXPERT_POOL_SIZE, key + " must have four basic, four advanced and two expert perks");
	}
}

std::optional<SkillDefinition> perkSkill(const JsonNode & rules, std::string_view skillId)
{
	validatePerkRules(rules);
	if(!usesPerkRules(rules))
		return std::nullopt;
	const auto it = rules["skills"].Struct().find(std::string(skillId));
	if(it == rules["skills"].Struct().end())
		return std::nullopt;
	SkillDefinition result;
	parseSkill(it->second, result);
	return result;
}

std::optional<PerkDefinition> perkDefinition(const JsonNode & rules, std::string_view skillId,
	std::string_view perkId)
{
	const auto skill = perkSkill(rules, skillId);
	if(!skill)
		return std::nullopt;
	const auto it = std::find_if(skill->perks.begin(), skill->perks.end(),
		[perkId](const auto & perk) { return perk.id == perkId; });
	if(it == skill->perks.end())
		return std::nullopt;
	return *it;
}

std::vector<PerkDefinition> perkOptions(const JsonNode & rules, std::string_view skillId)
{
	const auto skill = perkSkill(rules, skillId);
	return skill ? skill->perks : std::vector<PerkDefinition>();
}
}
