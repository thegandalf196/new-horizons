/*
 * NewHorizonsPerkState.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsPerkState.h"

#include <map>
#include <set>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
void savedFields(const JsonNode & node, std::initializer_list<std::string_view> keys)
{
	if(!node.isStruct())
		throw std::runtime_error("New Horizons perk state object required");
	for(const auto & [key, value] : node.Struct())
		if(std::find(keys.begin(), keys.end(), key) == keys.end())
			throw std::runtime_error("Unknown New Horizons perk state field " + key);
}
}

bool PerkState::hasSelection(const std::string & skillId, const std::string & perkId) const
{
	return std::any_of(selected.begin(), selected.end(), [&](const auto & entry)
	{
		return entry.skillId == skillId && entry.perkId == perkId;
	});
}

void PerkState::validate() const
{
	validatePerkRules(rules);
	if(!usesPerkRules(rules))
	{
		if(!selected.empty())
			throw std::runtime_error("Perk selections without a saved rules identity");
		return;
	}
	const auto cap = static_cast<size_t>(rules["maxPerksPerSkill"].Integer());
	std::set<std::pair<std::string, std::string>> identities;
	std::map<std::string, size_t> perSkill;
	for(const auto & entry : selected)
	{
		const auto definition = perkDefinition(rules, entry.skillId, entry.perkId);
		if(!definition || !identities.emplace(entry.skillId, entry.perkId).second
			|| ++perSkill[entry.skillId] > cap)
			throw std::runtime_error("Invalid saved New Horizons perk selection");
	}
}

void PerkState::select(const std::string & skillId, const std::string & perkId, int currentRank)
{
	validate();
	if(currentRank < 0 || currentRank > 3)
		throw std::runtime_error("Invalid New Horizons skill rank");
	const auto definition = perkDefinition(rules, skillId, perkId);
	if(!definition || currentRank < perkRequiredRank(definition->requiredRank) || hasSelection(skillId, perkId))
		throw std::runtime_error("Unavailable New Horizons perk selection");
	const auto count = std::count_if(selected.begin(), selected.end(), [&](const auto & entry)
	{
		return entry.skillId == skillId;
	});
	if(count >= rules["maxPerksPerSkill"].Integer())
		throw std::runtime_error("New Horizons perk cap reached for skill");
	selected.push_back({skillId, perkId});
	validate();
}

std::vector<PerkModifier> PerkState::project(
	const std::function<int(const std::string &)> & rankLookup) const
{
	validate();
	std::vector<PerkModifier> result;
	result.reserve(selected.size());
	for(const auto & entry : selected)
	{
		const auto definition = perkDefinition(rules, entry.skillId, entry.perkId);
		const int requiredRank = perkRequiredRank(definition->requiredRank);
		result.push_back({entry.skillId, entry.perkId, requiredRank,
			rankLookup(entry.skillId) >= requiredRank && definition->effect["status"].String() == "active",
			definition->effect});
	}
	return result;
}

JsonNode PerkState::toJson() const
{
	validate();
	if(!usesPerkRules(rules))
		return JsonNode();
	JsonNode result;
	result["stateVersion"].Integer() = 1;
	result["rules"] = rules;
	result["selected"].Vector();
	for(const auto & entry : selected)
	{
		JsonNode saved;
		saved["skillId"].String() = entry.skillId;
		saved["perkId"].String() = entry.perkId;
		result["selected"].Vector().push_back(std::move(saved));
	}
	return result;
}

PerkState PerkState::fromJson(const JsonNode & node)
{
	PerkState result;
	if(node.isNull() || (node.isStruct() && node.Struct().empty()))
		return result;
	savedFields(node, {"stateVersion", "rules", "selected"});
	if(node["stateVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| node["stateVersion"].Integer() != 1 || !node["selected"].isVector())
		throw std::runtime_error("Unsupported New Horizons perk state version or shape");
	result.rules = node["rules"];
	for(const auto & saved : node["selected"].Vector())
	{
		savedFields(saved, {"skillId", "perkId"});
		if(!saved["skillId"].isString() || !saved["perkId"].isString())
			throw std::runtime_error("Invalid New Horizons perk selection identity");
		result.selected.push_back({saved["skillId"].String(), saved["perkId"].String()});
	}
	result.validate();
	return result;
}
}
