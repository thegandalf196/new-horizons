/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkRules.h"
#include "../../network/InitializationProfile.h"
#include <algorithm>

namespace newHorizonsHeroes
{
namespace
{
void parsePerk(const JsonNode & node, PerkDefinition & result)
{
	result.id = node["id"].String();
	result.name = node["name"].String();
	result.requiredRank = node["requires"].String();
	result.description = node["description"].String();
	result.details = node["details"].isString() ? node["details"].String() : std::string{};
	result.effect = node["effect"];
}

void parseSkill(const JsonNode & node, SkillDefinition & result)
{
	result.id = node["id"].String();
	result.name = node["name"].String();
	result.domain = node["domain"].String();
	result.availability = node["availability"].String();
	result.description = node["description"].String();
	result.details = node["details"].isString() ? node["details"].String() : std::string{};
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

ValidatedPerkRules::ValidatedPerkRules(const JsonNode & captured) : rules(captured)
{
	validatePerkRules(rules);
	active = usesPerkRules(rules);
}

std::optional<PerkDefinition> ValidatedPerkRules::definition(std::string_view skillId,
	std::string_view perkId) const
{
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::PerkDefinition);
	if(!active)
		return std::nullopt;
	const auto skill = rules["skills"].Struct().find(std::string(skillId));
	if(skill == rules["skills"].Struct().end())
		return std::nullopt;
	const auto & perks = skill->second["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & row)
	{
		return row["id"].String() == perkId;
	});
	if(found == perks.end())
		return std::nullopt;
	PerkDefinition result;
	parsePerk(*found, result);
	return result;
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
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::PerkDefinition);
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
