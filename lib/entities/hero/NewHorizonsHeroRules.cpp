/*
 * NewHorizonsHeroRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "CHeroClassHandler.h"
#include "CHeroClass.h"
#include "../../GameLibrary.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include <cmath>
#include "../../callback/IGameInfoCallback.h"

const JsonNode & IGameInfoCallback::getHeroDevelopmentRules() const
{
	static const JsonNode legacy;
	return legacy;
}

namespace newHorizonsHeroes
{
namespace
{
void require(bool valid, const std::string & detail)
{
	if(!valid)
		throw std::runtime_error("Invalid New Horizons hero rules: " + detail);
}

bool integer(const JsonNode & value, int minimum, int maximum)
{
	return value.isNumber() && std::isfinite(value.Float()) && value.Float() >= minimum
		&& value.Float() <= maximum && std::floor(value.Float()) == value.Float();
}

int resolve(const std::string & type, const std::string & key)
{
	const auto separator = key.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < key.size(), "scoped " + type);
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), type, key, true);
	require(id.has_value() && *id >= 0, "unknown " + type + " " + key);
	return *id;
}

void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

void validateCommon(const JsonNode & rules)
{
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], HERO_RULESET_VERSION, HERO_RULESET_VERSION), "rulesetVersion");
	require(integer(rules["powerDivisor"], 1, 1000), "powerDivisor");
	require(integer(rules["maxPrimary"], 100, 1000000), "maxPrimary");
	require(rules["extraGrowth"].isVector(), "extraGrowth array");
	std::set<int> seen;
	for(const auto & extra : rules["extraGrowth"].Vector())
	{
		fields(extra, {"skill", "primary", "chances"});
		require(extra["skill"].isString(), "extra growth skill");
		require(seen.insert(resolve(SecondarySkill::entityType(), extra["skill"].String())).second, "duplicate extra skill");
		require(integer(extra["primary"], 0, GameConstants::PRIMARY_SKILLS - 1), "extra primary");
		require(extra["chances"].isVector() && extra["chances"].Vector().size() == 4, "four mastery chances");
		for(const auto & chance : extra["chances"].Vector())
			require(integer(chance, 0, 100), "chance percentage");
		require(extra["chances"].Vector().front().Integer() == 0, "unowned skill cannot grant extras");
	}
}

void validateProfile(const JsonNode & profile, int maximum)
{
	const auto parsed = parsePrimaryProfile(profile);
	for(const auto value : parsed.starting)
		require(value <= maximum, "starting rating exceeds maximum");
}
}

bool usesRules(const JsonNode & rules)
{
	return !rules.isNull() && !(rules.isStruct() && rules.Struct().empty());
}

void validateHeroRules(const JsonNode & rules, bool requireAllClasses)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "classProfiles", "extraGrowth"});
	validateCommon(rules);
	require(rules["classProfiles"].isStruct() && !rules["classProfiles"].Struct().empty(), "class profiles");
	std::set<int> seen;
	for(const auto & [key, profile] : rules["classProfiles"].Struct())
	{
		require(seen.insert(resolve(HeroClassID::entityType(), key)).second, "duplicate class");
		validateProfile(profile, rules["maxPrimary"].Integer());
	}
	if(requireAllClasses)
		for(const auto & heroClass : LIBRARY->heroclassesh->objects)
			if(heroClass)
				require(seen.count(heroClass->getIndex()), "missing class " + heroClass->getJsonKey());
}

void validateResolvedHeroRules(const JsonNode & rules)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "profile", "extraGrowth"});
	validateCommon(rules);
	validateProfile(rules["profile"], rules["maxPrimary"].Integer());
}

JsonNode resolveHeroRules(const JsonNode & rules, HeroClassID heroClass)
{
	if(!usesRules(rules))
		return JsonNode();
	JsonNode result;
	for(const auto * key : {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "extraGrowth"})
		result[key] = rules[key];
	result["profile"] = rules["classProfiles"][HeroClassID::encode(heroClass.getNum())];
	validateResolvedHeroRules(result);
	return result;
}

std::vector<SkillGrowthChance> skillGrowthChances(const JsonNode & resolvedRules,
	const std::function<int(SecondarySkill)> & rank)
{
	std::vector<SkillGrowthChance> result;
	if(!usesRules(resolvedRules))
		return result;
	for(const auto & extra : resolvedRules["extraGrowth"].Vector())
	{
		const SecondarySkill skill(resolve(SecondarySkill::entityType(), extra["skill"].String()));
		const int level = std::clamp(rank(skill), 0, 3);
		if(level > 0)
			result.push_back({skill, PrimarySkill(extra["primary"].Integer()), static_cast<int>(extra["chances"].Vector()[level].Integer())});
	}
	return result;
}
}
