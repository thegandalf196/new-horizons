/*
 * NewHorizonsCapabilityRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsCapabilityRules.h"
#include "NewHorizonsHeroRules.h"
#include "CHeroClass.h"
#include "CHeroClassHandler.h"
#include "../../GameLibrary.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"

#include <cmath>
#include <set>
#include <stdexcept>

const JsonNode & IGameInfoCallback::getHeroCapabilityRules() const
{
	static const JsonNode legacy;
	return legacy;
}

namespace newHorizonsHeroes
{
namespace
{
void require(bool condition, const std::string & detail)
{
	if(!condition)
		throw std::runtime_error("Invalid New Horizons capability rules: " + detail);
}

bool integer(const JsonNode & value, int minimum, int maximum)
{
	return value.isNumber() && std::isfinite(value.Float())
		&& value.Float() >= minimum && value.Float() <= maximum
		&& std::floor(value.Float()) == value.Float();
}

void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

int resolveClass(const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped class");
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), HeroClassID::entityType(), key, true);
	require(id.has_value() && *id >= 0, "unknown class " + key);
	return *id;
}

void rankValues(const JsonNode & node, int minimum, int maximum, int untrained)
{
	require(node.isVector() && node.Vector().size() == 4, "four skill rank values required");
	for(const auto & value : node.Vector())
		require(integer(value, minimum, maximum), "skill rank value");
	require(node.Vector().front().Integer() == untrained, "untrained skill value");
}

void common(const JsonNode & rules)
{
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], CAPABILITY_RULESET_VERSION, CAPABILITY_RULESET_VERSION), "rulesetVersion");
	fields(rules["leadership"], {"skillBonusPercent", "minimumMovementPercent"});
	rankValues(rules["leadership"]["skillBonusPercent"], 0, 1000, 0);
	require(integer(rules["leadership"]["minimumMovementPercent"], 1, 100), "minimumMovementPercent");
	fields(rules["siege"], {"ballistaDamageMultiplier"});
	rankValues(rules["siege"]["ballistaDamageMultiplier"], 1, 100, 1);
}

void profile(const JsonNode & node)
{
	fields(node, {"base", "perLevel"});
	require(integer(node["base"], 1, 1000000), "class leadership base");
	require(integer(node["perLevel"], 0, 1000000), "class leadership perLevel");
}

void resolvedRank(const JsonNode & rules, int rank)
{
	require(usesRules(rules), "resolved snapshot required for capability arithmetic");
	validateResolvedCapabilityRules(rules);
	require(rank >= 0 && rank <= 3, "skill rank outside NONE..EXPERT");
}
}

void validateCapabilityRules(const JsonNode & rules, bool requireAllClasses)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "classProfiles", "leadership", "siege"});
	common(rules);
	require(rules["classProfiles"].isStruct() && !rules["classProfiles"].Struct().empty(), "class profiles");
	std::set<int> seen;
	for(const auto & [key, value] : rules["classProfiles"].Struct())
	{
		require(seen.insert(resolveClass(key)).second, "duplicate resolved class");
		profile(value);
	}
	if(requireAllClasses)
		for(const auto & heroClass : LIBRARY->heroclassesh->objects)
			if(heroClass)
				require(seen.count(heroClass->getIndex()), "missing class " + heroClass->getJsonKey());
}

void validateResolvedCapabilityRules(const JsonNode & rules)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "profile", "leadership", "siege"});
	common(rules);
	profile(rules["profile"]);
}

JsonNode resolveCapabilityRules(const JsonNode & rules, HeroClassID heroClass)
{
	if(!usesRules(rules))
		return JsonNode();
	validateCapabilityRules(rules, false);
	JsonNode result;
	for(const auto * key : {"schemaVersion", "rulesetVersion", "leadership", "siege"})
		result[key] = rules[key];
	for(const auto & [key, value] : rules["classProfiles"].Struct())
		if(resolveClass(key) == heroClass.getNum())
			result["profile"] = value;
	validateResolvedCapabilityRules(result);
	return result;
}

LeadershipCapacity capabilityLeadership(const JsonNode & rules, int level, int leadershipRank, uint64_t used)
{
	resolvedRank(rules, leadershipRank);
	return leadershipCapacity(rules["profile"]["base"].Integer(), rules["profile"]["perLevel"].Integer(),
		level, rules["leadership"]["skillBonusPercent"].Vector()[leadershipRank].Integer(), used,
		rules["leadership"]["minimumMovementPercent"].Integer());
}

int capabilityBallistaMultiplier(const JsonNode & rules, int artilleryRank)
{
	resolvedRank(rules, artilleryRank);
	return rules["siege"]["ballistaDamageMultiplier"].Vector()[artilleryRank].Integer();
}
}
