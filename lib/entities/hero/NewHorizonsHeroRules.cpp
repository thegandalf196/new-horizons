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

void validateExcludedSkills(const JsonNode & excludedSkills)
{
	if(excludedSkills.isNull())
		return;

	require(excludedSkills.isVector(), "excludedSkills array");
	std::set<int> seen;
	for(const auto & skill : excludedSkills.Vector())
	{
		require(skill.isString() && !skill.String().empty(), "excluded skill");
		const auto skillId = resolve(SecondarySkill::entityType(), skill.String());
		require(seen.insert(skillId).second, "duplicate excluded skill " + skill.String());
	}
}

void validateSkillOfferWeightRow(const JsonNode & classWeights)
{
	require(classWeights.isStruct() && classWeights.Struct().size() == HERO_SKILL_OFFER_COUNT,
		"canonical skill offer table must contain exactly 31 skills");

	std::set<int> seenSkills;
	for(const auto & [skill, weight] : classWeights.Struct())
	{
		const auto skillId = resolve(SecondarySkill::entityType(), skill);
		require(seenSkills.insert(skillId).second, "duplicate skill offer skill");
		require(integer(weight, 0, 100), "skill offer weight");
	}
}

void validateSkillOfferWeights(const JsonNode & offerWeights, bool requireAllClasses)
{
	if(offerWeights.isNull())
		return;

	require(offerWeights.isStruct() && !offerWeights.Struct().empty(), "skill offer weights");
	std::set<int> seenClasses;
	for(const auto & [heroClass, classWeights] : offerWeights.Struct())
	{
		require(seenClasses.insert(resolve(HeroClassID::entityType(), heroClass)).second,
			"duplicate skill offer class");
		validateSkillOfferWeightRow(classWeights);
	}

	if(requireAllClasses)
		for(const auto & heroClass : LIBRARY->heroclassesh->objects)
			if(heroClass)
				require(seenClasses.count(heroClass->getIndex()),
					"missing skill offer table for " + heroClass->getJsonKey());
}

void validateStartingSkills(const JsonNode & startingSkills, bool requireMigrationTable)
{
	if(startingSkills.isNull())
		return;

	fields(startingSkills, {"factionSkills", "legacyAliases", "legacySkillMigrations", "magic", "might"});
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

	const auto & migrations = startingSkills["legacySkillMigrations"];
	if(migrations.isNull())
	{
		// Resolved snapshots written before creation-only migration was added do
		// not carry this table. They must remain loadable and retain their saved
		// roster; a complete new-game profile still requires the canonical table.
		require(!requireMigrationTable, "legacy starting-skill migrations");
	}
	else
	{
		require(migrations.isStruct() && !migrations.Struct().empty(), "legacy starting-skill migrations");
		std::set<int> migratedSources;
		for(const auto & [source, migration] : migrations.Struct())
		{
			require(migratedSources.insert(resolve(SecondarySkill::entityType(), source)).second,
				"duplicate legacy starting-skill migration " + source);
			fields(migration, {"kind", "target", "fallback"});
			require(migration["kind"].isString(), "legacy starting-skill migration kind");
			const auto kind = migration["kind"].String();
			require(kind == "skill" || kind == "factionSkill" || kind == "perk",
				"unknown legacy starting-skill migration kind " + kind);
			require(migration["target"].isString() && !migration["target"].String().empty(),
				"legacy starting-skill migration target");
			const auto target = migration["target"].String();
			const auto dot = target.find('.');
			if(kind == "perk")
			{
				require(dot != std::string::npos && dot > 0 && dot + 1 < target.size()
					&& target.find('.', dot + 1) == std::string::npos,
					"legacy perk migration target");
				require(resolve(SecondarySkill::entityType(), target.substr(0, dot)) >= 0,
					"unknown legacy perk migration skill " + target);
				require(migration["fallback"].String() == "remove",
					"legacy perk migration must document remove fallback");
			}
			else
			{
				require(dot == std::string::npos, "legacy skill migration target must be a skill");
				require(resolve(SecondarySkill::entityType(), target) >= 0,
					"unknown legacy skill migration target " + target);
				require(migration["fallback"].isNull(),
					"legacy skill migration cannot use a perk fallback");
			}
		}
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

bool usesSkillOfferWeights(const JsonNode & resolvedRules)
{
	return usesRules(resolvedRules) && resolvedRules["skillOfferWeights"].isStruct()
		&& !resolvedRules["skillOfferWeights"].Struct().empty();
}

std::optional<int> skillOfferWeight(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(!usesSkillOfferWeights(resolvedRules))
		return std::nullopt;

	const auto & weights = resolvedRules["skillOfferWeights"].Struct();
	const auto it = weights.find(SecondarySkill::encode(skill.getNum()));
	if(it == weights.end() || !integer(it->second, 0, 100))
		return std::nullopt;
	return static_cast<int>(it->second.Integer());
}

bool isExcludedSkill(const JsonNode & resolvedRules, SecondarySkill skill)
{
	const auto & excludedSkills = resolvedRules["excludedSkills"];
	if(!excludedSkills.isVector())
		return false;

	return std::any_of(excludedSkills.Vector().begin(), excludedSkills.Vector().end(),
		[skill](const JsonNode & entry)
		{
			return entry.isString() && SecondarySkill::decode(entry.String()) == skill.getNum();
		});
}

void validateHeroRules(const JsonNode & rules, bool requireAllClasses)
{
	if(!usesRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "classProfiles", "skillOfferWeights", "excludedSkills", "extraGrowth", "startingSkills"});
	validateCommon(rules);
	validateExcludedSkills(rules["excludedSkills"]);
	validateSkillOfferWeights(rules["skillOfferWeights"], requireAllClasses);
	validateStartingSkills(rules["startingSkills"], requireAllClasses);
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
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "profile", "skillOfferWeights", "excludedSkills", "extraGrowth", "startingSkills"});
	validateCommon(rules);
	validateExcludedSkills(rules["excludedSkills"]);
	if(!rules["skillOfferWeights"].isNull())
		validateSkillOfferWeightRow(rules["skillOfferWeights"]);
	validateStartingSkills(rules["startingSkills"], false);
	validateProfile(rules["profile"], rules["maxPrimary"].Integer());
}

JsonNode resolveHeroRules(const JsonNode & rules, HeroClassID heroClass)
{
	if(!usesRules(rules))
		return JsonNode();
	JsonNode result;
	for(const auto * key : {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "extraGrowth"})
		result[key] = rules[key];
	if(rules["skillOfferWeights"].isStruct())
		result["skillOfferWeights"] = rules["skillOfferWeights"][HeroClassID::encode(heroClass.getNum())];
	if(rules["excludedSkills"].isVector())
		result["excludedSkills"] = rules["excludedSkills"];
	if(rules["startingSkills"].isStruct())
		result["startingSkills"] = rules["startingSkills"];
	result["profile"] = rules["classProfiles"][HeroClassID::encode(heroClass.getNum())];
	validateResolvedHeroRules(result);
	return result;
}

std::vector<SkillGrowthChance> skillGrowthChances(const JsonNode & resolvedRules,
	const std::function<int(SecondarySkill)> & rank)
{
	// Keep the field accepted for saved-rules compatibility, but never turn
	// legacy extraGrowth rows into live primary-stat rolls. New Horizons grants
	// exactly the authored class vector on every level.
	(void)resolvedRules;
	(void)rank;
	return {};
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
void appendMergedSkill(std::vector<std::pair<SecondarySkill, ui8>> & result,
	SecondarySkill skill, ui8 rank)
{
	const auto existing = std::find_if(result.begin(), result.end(),
		[skill](const auto & value) { return value.first == skill; });
	if(existing == result.end())
		result.emplace_back(skill, rank);
	else
		existing->second = std::max(existing->second, rank);
}

std::vector<std::pair<SecondarySkill, ui8>> mergeStartingSkills(
	const std::vector<std::pair<SecondarySkill, ui8>> & skills)
{
	std::vector<std::pair<SecondarySkill, ui8>> result;
	result.reserve(skills.size());
	for(const auto & [skill, rank] : skills)
		appendMergedSkill(result, skill, rank);
	return result;
}

const JsonNode * findStartingSkillMigration(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return nullptr;
	const auto & migrations = resolvedRules["startingSkills"]["legacySkillMigrations"];
	if(!migrations.isStruct())
		return nullptr;
	const auto it = migrations.Struct().find(SecondarySkill::encode(skill.getNum()));
	return it == migrations.Struct().end() ? nullptr : &it->second;
}

SecondarySkill migrationTarget(const JsonNode & migration)
{
	const auto target = migration["target"].String();
	const auto dot = target.find('.');
	const auto skillName = target.substr(0, dot);
	const auto id = SecondarySkill::decode(skillName);
	if(id < 0)
		throw std::runtime_error("Invalid New Horizons starting-skill migration target " + target);
	return SecondarySkill(id);
}
}

std::optional<SecondarySkill> normalizeRewardSkill(const JsonNode & resolvedRules, SecondarySkill skill)
{
	if(skill == SecondarySkill::NONE || skill.getNum() < 0)
		return std::nullopt;

	if(!usesRules(resolvedRules))
		return skill;

	if(const auto * migration = findStartingSkillMigration(resolvedRules, skill))
	{
		const auto kind = (*migration)["kind"].String();
		if(kind == "skill")
			skill = migrationTarget(*migration);
		else
			// Faction skills need a hero-faction context and perk migrations are
			// not secondary skills. Reward loading has no such context, so fail
			// closed instead of leaking a retired core identity.
			return std::nullopt;
	}

	if(isExcludedSkill(resolvedRules, skill))
		return std::nullopt;

	return skill;
}

std::vector<std::pair<SecondarySkill, ui8>> migrateStartingSkills(
	const JsonNode & resolvedRules, FactionID faction,
	const std::vector<std::pair<SecondarySkill, ui8>> & initialSkills)
{
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return mergeStartingSkills(initialSkills);

	std::vector<std::pair<SecondarySkill, ui8>> result;
	result.reserve(initialSkills.size());
	for(const auto & [skill, rank] : initialSkills)
	{
		const auto * migration = findStartingSkillMigration(resolvedRules, skill);
		if(!migration)
		{
			appendMergedSkill(result, skill, rank);
			continue;
		}

		const auto kind = (*migration)["kind"].String();
		if(kind == "perk")
		{
			// Perk-at-start is not implemented. The authored target and explicit
			// remove fallback keep this honest: do not grant its parent Skill as a
			// proxy and do not retain a retired level-up identity.
			continue;
		}

		const auto target = migrationTarget(*migration);
		if(kind == "factionSkill")
		{
			// A faction target is meaningful only for the hero's own faction. A
			// malformed/custom cross-faction roster remains readable instead of
			// silently granting a foreign faction Skill.
			const auto ownFactionSkill = factionSkill(resolvedRules, faction);
			if(!ownFactionSkill || *ownFactionSkill != target)
			{
				appendMergedSkill(result, skill, rank);
				continue;
			}
		}
		appendMergedSkill(result, target, rank);
	}
	return result;
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
	std::vector<std::pair<SecondarySkill, ui8>> result = mergeStartingSkills(initialSkills);
	if(!usesRules(resolvedRules) || !resolvedRules["startingSkills"].isStruct())
		return result;

	const auto factionSkillId = factionSkill(resolvedRules, faction);
	if(!factionSkillId)
		return result;
	const auto & startingSkills = resolvedRules["startingSkills"];
	const SecondarySkill factionSkill = *factionSkillId;
	const SecondarySkill wisdomReplacement(SecondarySkill::decode(
		startingSkills["magic"]["replace"].String()));
	const SecondarySkill legacyWisdom = SecondarySkill::WISDOM;

	// Necromancy was already present in the legacy roster. Convert that legacy
	// identity to the New Horizons faction Skill instead of leaving two parallel
	// skills with the same player-facing name. If an authored roster already has
	// both identities, keep the first one's position and the strongest mastery.
	const auto legacyAliasId = legacyFactionSkillAlias(resolvedRules, faction);
	const SecondarySkill legacyAlias = legacyAliasId.value_or(SecondarySkill::NONE);
	std::vector<std::pair<SecondarySkill, ui8>> normalized;
	normalized.reserve(result.size());
	for(const auto & [skill, rank] : result)
	{
		if(skill == factionSkill || (legacyAlias != SecondarySkill::NONE && skill == legacyAlias))
		{
			appendMergedSkill(normalized, factionSkill, rank);
		}
		else
			appendMergedSkill(normalized, skill, rank);
	}
	result = std::move(normalized);

	if(magicHero)
	{
		const auto wisdomIt = std::find_if(result.begin(), result.end(),
			[wisdomReplacement, legacyWisdom](const auto & value)
			{
				// Accept the legacy identity as well as the canonical target. This
				// keeps direct callers and old resolved snapshots readable while
				// creation migration uses the scoped New Horizons identity.
				return value.first == wisdomReplacement || value.first == legacyWisdom;
			});
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

	// Wisdom is a Magic-only New Horizons Skill. A legacy Might hero can still
	// carry core:wisdom in its authored starting roster (Rashka is the shipped
	// example), but it must not leak that identity into a fresh hero. Spellcraft
	// is the generic, non-retired replacement for a Might hero's old magical
	// training; the faction Skill then occupies the authored second position.
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	std::vector<std::pair<SecondarySkill, ui8>> mightNormalized;
	mightNormalized.reserve(result.size());
	for(const auto & [skill, rank] : result)
	{
		if(skill == wisdomReplacement || skill == legacyWisdom)
			appendMergedSkill(mightNormalized, spellcraft, rank);
		else
			appendMergedSkill(mightNormalized, skill, rank);
	}
	result = std::move(mightNormalized);

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
