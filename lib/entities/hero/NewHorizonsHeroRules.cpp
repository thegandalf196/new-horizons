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

void validateStartingSkills(const JsonNode & startingSkills)
{
	if(startingSkills.isNull())
		return;

	fields(startingSkills, {"factionSkills", "legacyAliases", "magic", "might"});
	const auto & factionSkills = startingSkills["factionSkills"];
	require(factionSkills.isStruct() && !factionSkills.Struct().empty(), "faction starting skills");
	std::set<int> uniqueFactionSkills;
	for(const auto & [faction, skill] : factionSkills.Struct())
	{
		require(resolve(FactionID::entityType(), faction) >= 0, "unknown faction " + faction);
		require(skill.isString() && !skill.String().empty(), "faction starting skill");
		const auto skillId = resolve(SecondarySkill::entityType(), skill.String());
		require(skillId >= 0,
			"unknown faction starting skill " + skill.String());
		require(uniqueFactionSkills.insert(skillId).second, "duplicate faction starting skill " + skill.String());
	}

	const auto & legacyAliases = startingSkills["legacyAliases"];
	require(legacyAliases.isStruct(), "faction skill aliases");
	for(const auto & [faction, skill] : legacyAliases.Struct())
	{
		require(factionSkills.Struct().count(faction), "alias for unmapped faction " + faction);
		require(skill.isString() && !skill.String().empty(), "faction skill alias");
		require(resolve(SecondarySkill::entityType(), skill.String()) >= 0,
			"unknown faction skill alias " + skill.String());
	}

	const auto & magic = startingSkills["magic"];
	fields(magic, {"replace"});
	require(magic["replace"].isString() && !magic["replace"].String().empty(),
			"magic starting skill replacement");
	require(resolve(SecondarySkill::entityType(), magic["replace"].String()) >= 0,
			"unknown magic starting skill replacement " + magic["replace"].String());

	const auto & might = startingSkills["might"];
	fields(might, {"replacePosition", "singleSkillFallback"});
	require(might["replacePosition"].String() == "second", "might replacement position");
	require(might["singleSkillFallback"].String() == "append"
		|| might["singleSkillFallback"].String() == "replaceFirst", "might single-skill fallback");
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
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "classProfiles", "extraGrowth", "startingSkills"});
	validateCommon(rules);
	validateStartingSkills(rules["startingSkills"]);
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
	if(requireAllClasses && rules["startingSkills"].isStruct())
	{
		const auto & factionSkills = rules["startingSkills"]["factionSkills"];
		for(const auto & heroClass : LIBRARY->heroclassesh->objects)
			if(heroClass)
				require(factionSkills.Struct().count(FactionID::encode(heroClass->faction.getNum())),
					"missing faction starting skill for " + heroClass->getJsonKey());
	}
}

void validateResolvedHeroRules(const JsonNode & rules)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "profile", "extraGrowth", "startingSkills"});
	validateCommon(rules);
	validateStartingSkills(rules["startingSkills"]);
	validateProfile(rules["profile"], rules["maxPrimary"].Integer());
}

JsonNode resolveHeroRules(const JsonNode & rules, HeroClassID heroClass)
{
	if(!usesRules(rules))
		return JsonNode();
	JsonNode result;
	for(const auto * key : {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "extraGrowth"})
		result[key] = rules[key];
	if(rules["startingSkills"].isStruct())
		result["startingSkills"] = rules["startingSkills"];
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

std::optional<SecondarySkill> factionSkill(const JsonNode & resolvedRules, FactionID faction)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return std::nullopt;

	const auto & factionSkills = resolvedRules["startingSkills"]["factionSkills"];
	if(!factionSkills.isStruct())
		return std::nullopt;

	const auto skillName = factionSkills[FactionID::encode(faction.getNum())].String();
	if(skillName.empty())
		return std::nullopt;

	const auto skillId = SecondarySkill::decode(skillName);
	if(skillId < 0)
		return std::nullopt;
	return SecondarySkill(skillId);
}

namespace
{
std::optional<SecondarySkill> legacyFactionSkillAlias(const JsonNode & resolvedRules, FactionID faction)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return std::nullopt;

	const auto & aliases = resolvedRules["startingSkills"]["legacyAliases"];
	if(!aliases.isStruct())
		return std::nullopt;

	const auto aliasName = aliases[FactionID::encode(faction.getNum())].String();
	if(aliasName.empty())
		return std::nullopt;

	const auto aliasId = SecondarySkill::decode(aliasName);
	if(aliasId < 0)
		return std::nullopt;
	return SecondarySkill(aliasId);
}
}

bool isFactionSkillForFaction(const JsonNode & resolvedRules, FactionID faction, SecondarySkill skill)
{
	if(const auto canonical = factionSkill(resolvedRules, faction); canonical && *canonical == skill)
		return true;
	if(const auto alias = legacyFactionSkillAlias(resolvedRules, faction); alias && *alias == skill)
		return true;
	return false;
}

bool isFactionSkill(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return false;

	const auto & startingSkills = resolvedRules["startingSkills"];
	const auto & factionSkills = startingSkills["factionSkills"];
	if(factionSkills.isStruct())
		for(const auto & [faction, ignored] : factionSkills.Struct())
			if(isFactionSkillForFaction(resolvedRules,
				FactionID(FactionID::decode(faction)), skill))
				return true;
	return false;
}

std::vector<std::pair<SecondarySkill, ui8>> applyStartingFactionSkill(
	const JsonNode & resolvedRules, bool magicHero, FactionID faction,
	const std::vector<std::pair<SecondarySkill, ui8>> & initialSkills)
{
	std::vector<std::pair<SecondarySkill, ui8>> result = initialSkills;
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return result;

	const auto factionSkillId = factionSkill(resolvedRules, faction);
	if(!factionSkillId)
		return result;
	const auto & startingSkills = resolvedRules["startingSkills"];
	const SecondarySkill factionSkill = *factionSkillId;

	// Necromancy was already present in the legacy roster. Convert that legacy
	// identity to the New Horizons faction Skill instead of leaving two parallel
	// skills with the same player-facing name. If an authored roster already has
	// both identities, keep the first one's position and the strongest mastery.
	const auto legacyAliasId = legacyFactionSkillAlias(resolvedRules, faction);
	const SecondarySkill legacyAlias = legacyAliasId.value_or(SecondarySkill::NONE);
	std::vector<std::pair<SecondarySkill, ui8>> normalized;
	for(const auto & [skill, rank] : result)
	{
		if(skill == factionSkill || (legacyAlias != SecondarySkill::NONE && skill == legacyAlias))
		{
			auto existing = std::find_if(normalized.begin(), normalized.end(),
				[factionSkill](const auto & value) { return value.first == factionSkill; });
			if(existing == normalized.end())
			{
				normalized.emplace_back(factionSkill, rank);
			}
			else
				existing->second = std::max(existing->second, rank);
		}
		else
			normalized.emplace_back(skill, rank);
	}
	result = std::move(normalized);

	if(magicHero)
	{
		const SecondarySkill wisdom(SecondarySkill::decode(startingSkills["magic"]["replace"].String()));
		const auto wisdomIt = std::find_if(result.begin(), result.end(),
			[wisdom](const auto & value) { return value.first == wisdom; });
		const auto factionIt = std::find_if(result.begin(), result.end(),
			[factionSkill](const auto & value) { return value.first == factionSkill; });
		if(wisdomIt != result.end())
		{
			const auto wisdomIndex = static_cast<size_t>(std::distance(result.begin(), wisdomIt));
			const auto wisdomRank = wisdomIt->second;
			if(factionIt == result.end())
			{
				// Wisdom is replaced in-place so authored skill ordering remains
				// stable for both magic hero defaults and explicit map rosters.
				result[wisdomIndex] = {factionSkill,
					static_cast<ui8>(wisdomRank > 0 ? wisdomRank : static_cast<ui8>(MasteryLevel::BASIC))};
			}
			else
			{
				const auto factionIndex = static_cast<size_t>(std::distance(result.begin(), factionIt));
				result[factionIndex].second = std::max(result[factionIndex].second, wisdomRank);
				if(factionIndex > wisdomIndex)
				{
					result[wisdomIndex] = result[factionIndex];
					result.erase(result.begin() + static_cast<std::ptrdiff_t>(factionIndex));
				}
				else
				{
					result.erase(result.begin() + static_cast<std::ptrdiff_t>(wisdomIndex));
				}
			}
		}
		else if(factionIt == result.end())
			result.emplace_back(factionSkill, MasteryLevel::BASIC);
		return result;
	}

	if(std::any_of(result.begin(), result.end(),
		[factionSkill](const auto & value) { return value.first == factionSkill; }))
		return result;

	const auto & might = startingSkills["might"];
	const ui8 replacementRank = result.size() > 1 ? result[1].second : static_cast<ui8>(MasteryLevel::BASIC);
	if(might["replacePosition"].String() == "second" && result.size() > 1)
		result[1] = {factionSkill, replacementRank};
	else if(result.size() == 1 && might["singleSkillFallback"].String() == "replaceFirst")
		result[0] = {factionSkill, result[0].second};
	else
		result.emplace_back(factionSkill, replacementRank);
	return result;
}
}
