/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCapabilityRules.h"
#include "../../CCreatureHandler.h"
#include "../../GameLibrary.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
constexpr std::array<const char *, 10> PERKS = {"massedRanks", "veteranCadre", "familiarStandards",
	"foreignLegion", "officerCorps", "missileCommand", "regimentalOrganization", "combinedArms",
	"grandMarshal", "masterOfLegions"};
void require(bool value, const char * message)
{
	if(!value) throw std::runtime_error(message);
}
void percentage(const JsonNode & value)
{
	require(value.getType() == JsonNode::JsonType::DATA_INTEGER && value.Integer() >= 0
		&& value.Integer() <= 1000, "Invalid captured Leadership percentage");
}

using Creatures = std::map<CreatureID, const CCreature *>;
Creatures creatures()
{
	Creatures result;
	LIBRARY->creatures()->forEach([&](const Creature * creature, bool &)
	{
		if(const auto * native = dynamic_cast<const CCreature *>(creature))
			result.emplace(native->getId(), native);
	});
	return result;
}

std::map<CreatureID, int> authoredRequirements(const JsonNode & leadership)
{
	std::map<CreatureID, int> result;
	for(const auto & [key, value] : leadership["creatureRequirements"].Struct())
	{
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), CreatureID::entityType(), key, true);
		require(id.has_value(), "Unknown captured Leadership creature");
		require(result.emplace(CreatureID(*id), static_cast<int>(value.Integer())).second,
			"Duplicate captured Leadership creature");
	}
	return result;
}

CreatureID rootOf(CreatureID id, const std::map<CreatureID, CreatureID> & parents)
{
	for(size_t steps = 0; steps <= parents.size(); ++steps)
	{
		const auto found = parents.find(id);
		if(found == parents.end() || found->second == id) return id;
		id = found->second;
	}
	throw std::runtime_error("Cyclic Leadership creature-line metadata");
}
}

JsonNode resolveLeadershipEfficiencyMetadata(const JsonNode & leadership)
{
	JsonNode result = leadership;
	result.Struct().erase("resolvedCreatureRequirements");
	result.Struct().erase("resolvedCreatureLines");
	const auto entities = creatures();
	const auto authored = authoredRequirements(leadership);
	std::map<CreatureID, CreatureID> lines;
	std::map<CreatureID, std::set<CreatureID>> parents;
	for(const auto & [id, creature] : entities)
	{
		lines.try_emplace(id, id);
		for(const auto child : creature->upgrades)
		{
			parents[child].insert(id);
			const auto left = rootOf(id, lines), right = rootOf(child, lines);
			lines[std::max(left, right)] = std::min(left, right);
		}
	}
	const int64_t multiplier = leadership["upgradeMultiplierPercent"].Integer();
	const int64_t rounding = leadership["upgradeRounding"].Integer();
	for(const auto & [id, creature] : entities)
	{
		int64_t requirement = 50; // Existing fallback for unpriced optional creatures.
		std::set<CreatureID> frontier{id}, visited;
		size_t depth = 0;
		bool found = false;
		while(!frontier.empty() && !found)
		{
			std::set<CreatureID> next;
			for(const auto candidate : frontier)
			{
				if(!visited.insert(candidate).second) continue;
				if(const auto price = authored.find(candidate); price != authored.end())
				{
					requirement = price->second;
					for(size_t i = 0; i < depth; ++i)
						requirement = ((requirement * multiplier + 50 * rounding) / (100 * rounding)) * rounding;
					found = true;
					break;
				}
				if(const auto previous = parents.find(candidate); previous != parents.end())
					next.insert(previous->second.begin(), previous->second.end());
			}
			frontier = std::move(next);
			++depth;
		}
		require(requirement > 0 && requirement <= 1000000, "Invalid resolved Leadership Requirement");
		result["resolvedCreatureRequirements"][creature->getJsonKey()].Integer() = requirement;
		const auto root = entities.find(rootOf(id, lines));
		require(root != entities.end(), "Unknown captured Leadership creature line");
		result["resolvedCreatureLines"][creature->getJsonKey()].String() = root->second->getJsonKey();
	}
	return result;
}

void validateLeadershipEfficiencyPolicy(const JsonNode & leadership, bool resolved)
{
	const auto & policy = leadership["efficiencyPolicy"];
	require(policy.isStruct() && policy.Struct().size() == 3
		&& policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER && policy["version"].Integer() == 1,
		"Missing or unsupported Leadership Efficiency policy");
	const auto & ranks = policy["authorityRankBonusPercent"];
	require(ranks.isVector() && ranks.Vector().size() == 4, "Four Authority ranks required");
	for(const auto & value : ranks.Vector()) percentage(value);
	require(ranks.Vector().front().Integer() == 0, "Untrained Authority must be zero");
	const auto & perks = policy["authorityPerkBonusPercent"];
	require(perks.isStruct() && perks.Struct().size() == PERKS.size(), "Ten Authority perk values required");
	for(const auto * key : PERKS) percentage(perks[key]);
	if(!resolved)
	{
		require(!leadership.Struct().contains("resolvedCreatureRequirements")
			&& !leadership.Struct().contains("resolvedCreatureLines"), "Authored Leadership cannot contain derived metadata");
		return;
	}
	const auto expected = resolveLeadershipEfficiencyMetadata(leadership);
	require(leadership["resolvedCreatureRequirements"] == expected["resolvedCreatureRequirements"]
		&& leadership["resolvedCreatureLines"] == expected["resolvedCreatureLines"],
		"Stale or forged resolved Leadership metadata");
}

void validateLeadershipEfficiencySerialization(const JsonNode & rules, bool supported)
{
	if(rules.isStruct() && rules["rulesetVersion"].Integer() >= 7 && !supported)
		throw std::runtime_error("Leadership Efficiency requires format1159");
}
}
