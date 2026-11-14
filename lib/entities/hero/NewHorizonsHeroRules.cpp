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
#include "NewHorizonsPerkRules.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "NewHorizonsSwampSeer.h"
#include "HeroStartingProjection.h"
#include "NewHorizonsRuleValidation.h"
#include "NewHorizonsSkillSpecialtyRules.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../CCreatureHandler.h"
#include "../../CSkillHandler.h"
#include "../../texts/CGeneralTextHandler.h"
#include "../../texts/MetaString.h"
#include "CHeroClassHandler.h"
#include "CHeroClass.h"
#include "CHeroHandler.h"
#include "../../spells/CSpellHandler.h"
#include "../../spells/CSpell.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsSpellAvailability.h"
#include "../../GameLibrary.h"
#include "../../modding/IdentifierStorage.h"
#include "../../modding/ModScope.h"
#include <cmath>
#include "../../callback/IGameInfoCallback.h"
#include "../../pathfinder/NewHorizonsLighthouse.h"
#include "../../pathfinder/NewHorizonsMovement.h"

const JsonNode & IGameInfoCallback::getHeroDevelopmentRules() const
{
	static const JsonNode legacy;
	return legacy;
}

namespace newHorizonsHeroes
{
namespace
{
using detail::fields;
using detail::integer;
using detail::require;
using detail::resolve;
using detail::validateOptionalSkillSpecialtyRules;
using detail::validateStartingSkills;

void validateCommon(const JsonNode & rules)
{
	newHorizonsMovement::validateDailyMovementRules(rules);
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], HERO_RULESET_VERSION, HERO_RULESET_VERSION), "rulesetVersion");
	require(integer(rules["powerDivisor"], 1, 1000), "powerDivisor");
	require(integer(rules["maxPrimary"], 100, 1000000), "maxPrimary");
	if(rules.Struct().contains("skillOfferLayoutVersion"))
		require(rules["skillOfferLayoutVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& integer(rules["skillOfferLayoutVersion"], 1, 1) && rules["skillOfferWeights"].isStruct(),
			"split Skill offers require captured class weights");
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

void validateCreatureLineSpecialtyRules(const JsonNode & rules)
{
	fields(rules, {"version", "speed", "initiative", "attributePerStep", "levelStep", "attributeMaximum"});
	require(integer(rules["version"], 1, 1), "creature-line specialty version");
	require(integer(rules["speed"], 1, 1), "creature-line specialty speed");
	require(integer(rules["initiative"], 1, 1), "creature-line specialty initiative");
	require(integer(rules["attributePerStep"], 1, 1), "creature-line specialty attribute step");
	require(integer(rules["levelStep"], 5, 5), "creature-line specialty level step");
	require(integer(rules["attributeMaximum"], 6, 6), "creature-line specialty attribute maximum");
}

void validateOptionalCreatureLineSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return;
	const auto found = rules.Struct().find("creatureLineSpecialties");
	if(found != rules.Struct().end())
		validateCreatureLineSpecialtyRules(found->second);
}

void validateDamageSpellSpecialtyRules(const JsonNode & rules)
{
	fields(rules, {"version", "componentPercent", "coroniusHolyWrathReplacement"});
	const auto & replacement = rules["coroniusHolyWrathReplacement"];
	require(!rules.Struct().contains("coroniusHolyWrathReplacement") || replacement.isBool(),
		"Coronius Holy Wrath replacement flag");
	require(integer(rules["version"], 1, 1), "damage-spell specialty version");
	require(integer(rules["componentPercent"], 15, 15), "damage-spell specialty component percentage");
}

void validateOptionalDamageSpellSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return;
	const auto found = rules.Struct().find("damageSpellSpecialties");
	if(found != rules.Struct().end())
		validateDamageSpellSpecialtyRules(found->second);
}

void validateNonDamageSpellSpecialtyRules(const JsonNode & rules)
{
	fields(rules, {"version", "componentPercent", "spells", "aenainFrailtyReplacement", "defensiveStartReplacements", "offensiveStartReplacements", "remainingStartReplacements"});
	const auto & aenain = rules["aenainFrailtyReplacement"];
	require(!rules.Struct().contains("aenainFrailtyReplacement") || aenain.isBool(),
		"Aenain Frailty replacement flag");
	const auto & remaining = rules["remainingStartReplacements"];
	require(!rules.Struct().contains("remainingStartReplacements") || remaining.isBool(),
		"remaining starting inscription replacement flag");
	const auto & defensive = rules["defensiveStartReplacements"];
	require(!rules.Struct().contains("defensiveStartReplacements") || defensive.isBool(),
		"defensive starting specialty replacement flag");
	const auto & offensive = rules["offensiveStartReplacements"];
	require(!rules.Struct().contains("offensiveStartReplacements") || offensive.isBool(),
		"offensive starting specialty replacement flag");
	require(integer(rules["version"], 1, 1), "non-damage-spell specialty version");
	require(integer(rules["componentPercent"], 20, 20), "non-damage-spell specialty component percentage");
	const auto & spells = rules["spells"];
	require(spells.isVector() && !spells.Vector().empty() && spells.Vector().size() <= 11,
		"version 1 non-damage spell specialties must list one to eleven supported spells");
	if(aenain.isBool() && aenain.Bool())
		require(std::ranges::any_of(spells.Vector(), [](const JsonNode & spell)
		{
			return spell.isString() && spell.String() == "new-horizons:frailty";
		}), "Aenain replacement requires Frailty specialty rules");
	if(remaining.isBool() && remaining.Bool())
		require(std::ranges::any_of(spells.Vector(), [](const JsonNode & spell)
		{
			return spell.isString() && spell.String() == "new-horizons:crusade";
		}), "Inteus replacement requires Crusade specialty identity");
	if(defensive.isBool() && defensive.Bool())
		for(const auto key : {"new-horizons:hydrasVitality", "new-horizons:guardianSpirit"})
			require(std::ranges::any_of(spells.Vector(), [key](const JsonNode & spell)
			{
				return spell.isString() && spell.String() == key;
			}), "defensive replacements require both authored spell specialty identities");
	if(offensive.isBool() && offensive.Bool())
		for(const auto key : {"new-horizons:crusade", "new-horizons:focusMagic"})
			require(std::ranges::any_of(spells.Vector(), [key](const JsonNode & spell)
			{
				return spell.isString() && spell.String() == key;
			}), "offensive replacements require both authored spell specialty identities");
	std::set<int> seen;
	for(const auto & spell : spells.Vector())
	{
		require(spell.isString()
			&& (spell.String() == "core:cure" || spell.String() == "core:resurrection"
				|| spell.String() == "core:bless" || spell.String() == "core:haste"
				|| spell.String() == "new-horizons:reanimate" || spell.String() == "new-horizons:frailty"
				|| spell.String() == "new-horizons:hydrasVitality" || spell.String() == "new-horizons:guardianSpirit"
				|| spell.String() == "new-horizons:crusade" || (spell.String() == "new-horizons:focusMagic" || spell.String() == "new-horizons:phantomArmy")),
			"Unsupported version 1 non-damage spell specialty");
		const int spellId = resolve("spell", spell.String());
		require(spellId == SpellID::CURE || spellId == SpellID::RESURRECTION
			|| spellId == SpellID::BLESS || spellId == SpellID::HASTE
			|| spell.String() == "new-horizons:reanimate" || spell.String() == "new-horizons:frailty"
			|| spell.String() == "new-horizons:hydrasVitality" || spell.String() == "new-horizons:guardianSpirit"
			|| spell.String() == "new-horizons:crusade" || (spell.String() == "new-horizons:focusMagic" || spell.String() == "new-horizons:phantomArmy"),
			"unknown version 1 non-damage spell specialty");
		require(seen.insert(spellId).second, "duplicate version 1 non-damage spell specialty");
	}
}

void validateOptionalNonDamageSpellSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return;
	const auto found = rules.Struct().find("nonDamageSpellSpecialties");
	if(found != rules.Struct().end())
		validateNonDamageSpellSpecialtyRules(found->second);
}

void validateDruidSpecialtyRules(const JsonNode & rules)
{
	fields(rules, {"version", "highLore", "restorativeSorceress", "naturalPhilosopher"});
	require(integer(rules["version"], 1, 1), "Druid specialty version");
	for(const char * key : {"highLore", "restorativeSorceress", "naturalPhilosopher"})
	{
		const auto & row = rules[key];
		if(std::string_view(key) == "naturalPhilosopher")
		{
			fields(row, {"percentPerLevel", "maximumPercent", "knowledgeObjectExperiencePercent"});
			require(integer(row["knowledgeObjectExperiencePercent"], 0, 1000), "knowledge-object XP percentage");
		}
		else
			fields(row, {"percentPerLevel", "maximumPercent"});
		require(integer(row["percentPerLevel"], 0, 1000), "Druid specialty percentage per level");
		require(integer(row["maximumPercent"], 0, 1000), "Druid specialty maximum percentage");
	}
}



void validateOptionalBottomlessMindSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct() || !rules.Struct().contains("bottomlessMindSpecialty"))
		return;
	const auto & row = rules["bottomlessMindSpecialty"];
	fields(row, {"version", "pointsPerLevel"});
	require(integer(row["version"], 1, 1), "Bottomless Mind version");
	require(integer(row["pointsPerLevel"], 0, 1000000), "Bottomless Mind points per level");
}

void validateOptionalMasterSpellwardSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct() || !rules.Struct().contains("masterSpellwardSpecialty"))
		return;
	const auto & row = rules["masterSpellwardSpecialty"];
	fields(row, {"version", "percentPerLevel", "maximumExtraPercent"});
	require(integer(row["version"], 1, 1), "Master Spellward version");
	require(integer(row["percentPerLevel"], 0, 100), "Master Spellward percentage per level");
	require(integer(row["maximumExtraPercent"], 0, 100), "Master Spellward maximum extra percentage");
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

void validateSkillOfferWeights(const JsonNode & rules, bool requireAllClasses)
{
	const auto & offerWeights = rules["skillOfferWeights"];
	if(usesCommandAuthorityPolicy(rules))
		require(offerWeights.isStruct(), "Command/Authority requires its complete offer table");
	if(offerWeights.isNull())
		return;

	require(offerWeights.isStruct() && !offerWeights.Struct().empty(), "skill offer weights");
	std::set<int> seenClasses;
	for(const auto & [heroClass, classWeights] : offerWeights.Struct())
	{
		require(seenClasses.insert(resolve(HeroClassID::entityType(), heroClass)).second,
			"duplicate skill offer class");
		validateSkillOfferWeightRow(classWeights, rules);
	}

	if(requireAllClasses)
		for(const auto & heroClass : LIBRARY->heroclassesh->objects)
			if(heroClass)
				require(seenClasses.count(heroClass->getIndex()),
					"missing skill offer table for " + heroClass->getJsonKey());
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

bool usesSplitSkillOffers(const JsonNode & rules)
{
	return usesSkillOfferWeights(rules) && rules["skillOfferLayoutVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["skillOfferLayoutVersion"].Integer() == 1;
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
	usesCommandAuthorityPolicy(rules);
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "classProfiles", "skillOfferWeights", "excludedSkills", "extraGrowth", "startingSkills", "creatureLineSpecialties", "damageSpellSpecialties", "nonDamageSpellSpecialties", "skillSpecialties", "druidSpecialties", "remainingSpellSpecialtyReplacements", "lighthouseDeparture", "defaultCreatureLineReplacements", "fortuneChildSpecialty", "arcanePreparationSpecialty", "bottomlessMindSpecialty", "masterSpellwardSpecialty", "individualSpecialties", "schoolSpellcraftSpecialties", "additionalSpecialties", "recoverySpecialties", "learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty", "ghostSpecialty", "petrifyingBloodSpecialty", "livingInfernoSpecialty", "dreadCavalrySpecialty", "genieSageSpecialties", "chosenSpecialties", "commandAuthorityPolicy", "skillOfferLayoutVersion", "dailyMovement"});
	validateCurrentSpecialtyRules(rules);
	validateOptionalBottomlessMindSpecialtyRules(rules);
	validateOptionalMasterSpellwardSpecialtyRules(rules);
	newHorizonsLighthouse::validateRulesSerialization(rules, true);
	validateCommon(rules);
	validateOptionalCreatureLineSpecialtyRules(rules);
	validateOptionalDamageSpellSpecialtyRules(rules);
	validateOptionalNonDamageSpellSpecialtyRules(rules);
	validateOptionalSkillSpecialtyRules(rules);
	validateDruidSpecialtySerialization(rules, true);
	validateRemainingSpellSpecialtySerialization(rules, true);
	validateDefaultCreatureLineSerialization(rules, true);
	if(rules.Struct().contains("defaultCreatureLineReplacements"))
		require(creatureLineSpecialtyRules(rules).has_value(), "default successor needs creature-line coefficients");
	validateExcludedSkills(rules["excludedSkills"]);
	validateSkillOfferWeights(rules, requireAllClasses);
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
	usesCommandAuthorityPolicy(rules);
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "profile", "skillOfferWeights", "excludedSkills", "extraGrowth", "startingSkills", "creatureLineSpecialties", "damageSpellSpecialties", "nonDamageSpellSpecialties", "skillSpecialties", "druidSpecialties", "remainingSpellSpecialtyReplacements", "lighthouseDeparture", "defaultCreatureLineReplacements", "creatureLineSpecialtyTarget", "fortuneChildSpecialty", "arcanePreparationSpecialty", "bottomlessMindSpecialty", "masterSpellwardSpecialty", "individualSpecialties", "schoolSpellcraftSpecialties", "additionalSpecialties", "recoverySpecialties", "learningExchangeSpecialties", "swampSeerSpecialty", "reclamationSpecialty", "ghostSpecialty", "petrifyingBloodSpecialty", "livingInfernoSpecialty", "dreadCavalrySpecialty", "genieSageSpecialties", "chosenSpecialties", "commandAuthorityPolicy", "skillOfferLayoutVersion", "dailyMovement"});
	validateCurrentSpecialtyRules(rules);
	validateOptionalBottomlessMindSpecialtyRules(rules);
	validateOptionalMasterSpellwardSpecialtyRules(rules);
	newHorizonsLighthouse::validateRulesSerialization(rules, true);
	validateCommon(rules);
	validateOptionalCreatureLineSpecialtyRules(rules);
	validateOptionalDamageSpellSpecialtyRules(rules);
	validateOptionalNonDamageSpellSpecialtyRules(rules);
	validateOptionalSkillSpecialtyRules(rules);
	validateDruidSpecialtySerialization(rules, true);
	validateRemainingSpellSpecialtySerialization(rules, true);
	validateDefaultCreatureLineSerialization(rules, true);
	if(rules.Struct().contains("defaultCreatureLineReplacements"))
		require(creatureLineSpecialtyRules(rules).has_value(), "default successor needs creature-line coefficients");
	validateExcludedSkills(rules["excludedSkills"]);
	if(usesCommandAuthorityPolicy(rules) || !rules["skillOfferWeights"].isNull())
		validateSkillOfferWeightRow(rules["skillOfferWeights"], rules);
	validateStartingSkills(rules["startingSkills"], false);
	validateProfile(rules["profile"], rules["maxPrimary"].Integer());
}

JsonNode resolveHeroRules(const JsonNode & rules, HeroClassID heroClass)
{
	if(!usesRules(rules))
		return JsonNode();
	JsonNode result;
	if(rules.Struct().contains("commandAuthorityPolicy"))
		result["commandAuthorityPolicy"] = rules["commandAuthorityPolicy"];
	if(rules.Struct().contains("skillOfferLayoutVersion"))
		result["skillOfferLayoutVersion"] = rules["skillOfferLayoutVersion"];
	for(const auto * key : {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "extraGrowth"})
		result[key] = rules[key];
	if(rules.Struct().contains("defaultCreatureLineReplacements"))
		result["defaultCreatureLineReplacements"] = rules["defaultCreatureLineReplacements"];
	if(rules.Struct().contains("creatureLineSpecialties"))
		result["creatureLineSpecialties"] = rules["creatureLineSpecialties"];
	if(rules.Struct().contains("damageSpellSpecialties"))
		result["damageSpellSpecialties"] = rules["damageSpellSpecialties"];
	if(rules.Struct().contains("nonDamageSpellSpecialties"))
		result["nonDamageSpellSpecialties"] = rules["nonDamageSpellSpecialties"];
	if(rules.Struct().contains("remainingSpellSpecialtyReplacements"))
		result["remainingSpellSpecialtyReplacements"] = rules["remainingSpellSpecialtyReplacements"];
	if(rules.Struct().contains("skillSpecialties"))
		result["skillSpecialties"] = rules["skillSpecialties"];
	copyCapturedIndividualSpecialties(result, rules);
	if(rules.Struct().contains("druidSpecialties"))
		result["druidSpecialties"] = rules["druidSpecialties"];
	if(rules.Struct().contains("fortuneChildSpecialty"))
		result["fortuneChildSpecialty"] = rules["fortuneChildSpecialty"];
	if(rules.Struct().contains("arcanePreparationSpecialty"))
		result["arcanePreparationSpecialty"] = rules["arcanePreparationSpecialty"];
	if(rules.Struct().contains("bottomlessMindSpecialty"))
		result["bottomlessMindSpecialty"] = rules["bottomlessMindSpecialty"];
	if(rules.Struct().contains("masterSpellwardSpecialty"))
		result["masterSpellwardSpecialty"] = rules["masterSpellwardSpecialty"];
	if(rules.Struct().contains("lighthouseDeparture"))
		result["lighthouseDeparture"] = rules["lighthouseDeparture"];
	if(rules.Struct().contains("dailyMovement"))
		result["dailyMovement"] = rules["dailyMovement"];
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

void validateDruidSpecialtySerialization(const JsonNode & development, bool supported)
{
	const auto & rules = development["druidSpecialties"];
	if(rules.isNull())
		return;
	validateDruidSpecialtyRules(rules);
	if(!supported)
		throw std::runtime_error("Captured Druid specialties require the new save format");
}

std::optional<DruidSpecialtyRules> druidSpecialtyRules(const JsonNode & development)
{
	if(!usesRules(development) || development["druidSpecialties"].isNull())
		return std::nullopt;
	validateDruidSpecialtySerialization(development, true);
	const auto & rules = development["druidSpecialties"];
	const auto levelRule = [](const JsonNode & row)
	{
		return LevelScaledSpecialty{static_cast<int>(row["percentPerLevel"].Integer()),
			static_cast<int>(row["maximumPercent"].Integer())};
	};
	return DruidSpecialtyRules{1, levelRule(rules["highLore"]), levelRule(rules["restorativeSorceress"]),
		levelRule(rules["naturalPhilosopher"]),
		static_cast<int>(rules["naturalPhilosopher"]["knowledgeObjectExperiencePercent"].Integer())};
}

int druidSpecialtyPercent(int level, const LevelScaledSpecialty & rule)
{
	if(level < 0 || rule.percentPerLevel < 0 || rule.percentPerLevel > 1000
		|| rule.maximumPercent < 0 || rule.maximumPercent > 1000)
		throw std::invalid_argument("Invalid Druid specialty level coefficients");
	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(level) * rule.percentPerLevel,
		rule.maximumPercent));
}

int64_t druidSpecialtyExtra(int64_t ordinaryContribution, int percent)
{
	if(ordinaryContribution < 0 || percent < 0 || percent > 1000)
		throw std::invalid_argument("Invalid Druid specialty contribution");
	if(percent == 0)
		return 0;
	const auto maximum = std::numeric_limits<int64_t>::max();
	const auto whole = ordinaryContribution / 100;
	if(whole > maximum / percent)
		return maximum;
	const auto result = whole * percent;
	const auto remainder = ordinaryContribution % 100 * percent / 100;
	return result > maximum - remainder ? maximum : result + remainder;
}

std::optional<CreatureLineSpecialtyRules> creatureLineSpecialtyRules(const JsonNode & resolvedRules)
{
	if(!usesRules(resolvedRules) || !resolvedRules.isStruct())
		return std::nullopt;
	const auto found = resolvedRules.Struct().find("creatureLineSpecialties");
	if(found == resolvedRules.Struct().end())
		return std::nullopt;
	validateCreatureLineSpecialtyRules(found->second);
	const auto & rules = found->second;
	return CreatureLineSpecialtyRules{
		.version = static_cast<int>(rules["version"].Integer()),
		.speed = static_cast<int>(rules["speed"].Integer()),
		.initiative = static_cast<int>(rules["initiative"].Integer()),
		.attributePerStep = static_cast<int>(rules["attributePerStep"].Integer()),
		.levelStep = static_cast<int>(rules["levelStep"].Integer()),
		.attributeMaximum = static_cast<int>(rules["attributeMaximum"].Integer())
	};
}

std::optional<DamageSpellSpecialtyRules> damageSpellSpecialtyRules(const JsonNode & resolvedRules)
{
	if(!usesRules(resolvedRules) || !resolvedRules.isStruct())
		return std::nullopt;
	const auto found = resolvedRules.Struct().find("damageSpellSpecialties");
	if(found == resolvedRules.Struct().end())
		return std::nullopt;
	validateDamageSpellSpecialtyRules(found->second);
	return DamageSpellSpecialtyRules{
		.version = static_cast<int>(found->second["version"].Integer()),
		.componentPercent = static_cast<int>(found->second["componentPercent"].Integer())
	};
}

bool hasHasteSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return false;
	const auto & specialties = rules["nonDamageSpellSpecialties"];
	if(!specialties.isStruct() || !specialties["spells"].isVector())
		return false;
	return std::ranges::any_of(specialties["spells"].Vector(), [](const JsonNode & spell)
	{
		return spell.isString() && spell.String() == "core:haste";
	});
}

void validateHasteSpecialtySerialization(const JsonNode & rules, bool supported)
{
	if(!supported && hasHasteSpecialtyRules(rules))
		throw std::runtime_error("Haste specialty rules require the new save format");
}

bool hasReanimateSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return false;
	const auto & specialties = rules["nonDamageSpellSpecialties"];
	if(!specialties.isStruct() || !specialties["spells"].isVector())
		return false;
	return std::ranges::any_of(specialties["spells"].Vector(), [](const JsonNode & spell)
	{
		return spell.isString() && spell.String() == "new-horizons:reanimate";
	});
}

void validateReanimateSpecialtySerialization(const JsonNode & rules, bool supported)
{
	if(!supported && hasReanimateSpecialtyRules(rules))
		throw std::runtime_error("Re-animate specialty rules require the new save format");
}

bool hasFrailtySpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return false;
	const auto & specialties = rules["nonDamageSpellSpecialties"];
	if(!specialties.isStruct() || !specialties["spells"].isVector())
		return false;
	return std::ranges::any_of(specialties["spells"].Vector(), [](const JsonNode & spell)
	{
		return spell.isString() && spell.String() == "new-horizons:frailty";
	});
}

void validateFrailtySpecialtySerialization(const JsonNode & rules, bool supported)
{
	if(!supported && hasFrailtySpecialtyRules(rules))
		throw std::runtime_error("Frailty specialty rules require the new save format");
}

void validateAenainFrailtySpecialtySerialization(const JsonNode & rules, bool supported)
{
	const auto & specialties = rules["nonDamageSpellSpecialties"];
	if(!supported && specialties.isStruct() && specialties.Struct().contains("aenainFrailtyReplacement"))
		throw std::runtime_error("Aenain Frailty specialty rules require the new save format");
}

void validateDefensiveStartSpecialtySerialization(const JsonNode & rules, bool supported)
{
	const auto & specialties = rules["nonDamageSpellSpecialties"];
	const auto & spells = specialties["spells"];
	const bool hasNewSpell = spells.isVector() && std::ranges::any_of(spells.Vector(), [](const JsonNode & spell)
	{
		return spell.isString() && (spell.String() == "new-horizons:hydrasVitality"
			|| spell.String() == "new-horizons:guardianSpirit");
	});
	if(!supported && ((specialties.isStruct() && specialties.Struct().contains("defensiveStartReplacements")) || hasNewSpell))
		throw std::runtime_error("Defensive starting specialty rules require the new save format");
}

void validateOffensiveStartSpecialtySerialization(const JsonNode & rules, bool supported)
{
	const auto & specialties = rules["nonDamageSpellSpecialties"];
	const auto & spells = specialties["spells"];
	const bool hasNewSpell = spells.isVector() && std::ranges::any_of(spells.Vector(), [](const JsonNode & spell)
	{
		return spell.isString() && (spell.String() == "new-horizons:crusade"
			|| (spell.String() == "new-horizons:focusMagic" || spell.String() == "new-horizons:phantomArmy"));
	});
	if(!supported && ((specialties.isStruct() && specialties.Struct().contains("offensiveStartReplacements")) || hasNewSpell))
		throw std::runtime_error("Offensive starting specialty rules require the new save format");
}




std::optional<BottomlessMindSpecialtyRules> bottomlessMindSpecialtyRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("bottomlessMindSpecialty"))
		return std::nullopt;
	validateOptionalBottomlessMindSpecialtyRules(development);
	return BottomlessMindSpecialtyRules{static_cast<int>(development["bottomlessMindSpecialty"]["pointsPerLevel"].Integer())};
}

void validateBottomlessMindSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("bottomlessMindSpecialty"))
		return;
	validateOptionalBottomlessMindSpecialtyRules(development);
	if(!supported)
		throw std::runtime_error("Captured Bottomless Mind requires the new save format");
}

std::optional<MasterSpellwardSpecialtyRules> masterSpellwardSpecialtyRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("masterSpellwardSpecialty"))
		return std::nullopt;
	validateOptionalMasterSpellwardSpecialtyRules(development);
	const auto & row = development["masterSpellwardSpecialty"];
	return MasterSpellwardSpecialtyRules{static_cast<int>(row["percentPerLevel"].Integer()),
		static_cast<int>(row["maximumExtraPercent"].Integer())};
}

void validateMasterSpellwardSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("masterSpellwardSpecialty"))
		return;
	validateOptionalMasterSpellwardSpecialtyRules(development);
	if(!supported)
		throw std::runtime_error("Captured Master Spellward requires the new save format");
}

bool usesRemainingSpellSpecialties(const JsonNode & rules)
{
	return usesRules(rules) && rules["remainingSpellSpecialtyReplacements"].isBool()
		&& rules["remainingSpellSpecialtyReplacements"].Bool();
}

void validateRemainingSpellSpecialtySerialization(const JsonNode & rules, bool supported)
{
	const bool present = rules.isStruct() && rules.Struct().contains("remainingSpellSpecialtyReplacements");
	const auto & spells = rules["nonDamageSpellSpecialties"]["spells"];
	const bool phantom = spells.isVector() && std::ranges::any_of(spells.Vector(), [](const JsonNode & spell)
	{
		return spell.isString() && spell.String() == "new-horizons:phantomArmy";
	});
	if(!supported && (present || phantom))
		throw std::runtime_error("Remaining spell specialties require the new save format");
	if(present)
	{
		require(rules["remainingSpellSpecialtyReplacements"].isBool(), "remaining spell specialty opt-in boolean");
		if(rules["remainingSpellSpecialtyReplacements"].Bool())
		{
			require(rules["damageSpellSpecialties"].isStruct(), "remaining damage specialty rules");
			validateDamageSpellSpecialtyRules(rules["damageSpellSpecialties"]);
			require(rules["nonDamageSpellSpecialties"].isStruct() && phantom, "remaining Phantom specialty rules");
			validateNonDamageSpellSpecialtyRules(rules["nonDamageSpellSpecialties"]);
			for(const auto * identity : {"core:bless", "core:haste", "new-horizons:phantomArmy"})
				require(std::ranges::any_of(spells.Vector(), [identity](const JsonNode & spell)
					{ return spell.isString() && spell.String() == identity; }),
					"remaining duration and Integrity specialty identities");
		}
	}
}

void validateCoroniusHolyWrathSerialization(const JsonNode & rules, bool supported)
{
	const auto & specialties = rules["damageSpellSpecialties"];
	if(!supported && specialties.isStruct() && specialties.Struct().contains("coroniusHolyWrathReplacement"))
		throw std::runtime_error("Coronius Holy Wrath replacement rules require the new save format");
}

void validateDefaultCreatureLineSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct())
		return;
	const bool table = rules.Struct().contains("defaultCreatureLineReplacements");
	const bool target = rules.Struct().contains("creatureLineSpecialtyTarget");
	if(!supported && (table || target))
		throw std::runtime_error("Default creature-line successor requires the new save format");
	if(table)
	{
		const auto & replacements = rules["defaultCreatureLineReplacements"];
		fields(replacements, {"core:pasis", "core:monere"});
		require(replacements.Struct().size() == 2, "both default Wisp specialties required");
		for(const auto * hero : {"core:pasis", "core:monere"})
			require(replacements[hero].isString() && replacements[hero].String() == "new-horizons:wisp", "exact default Wisp target");
	}
	if(target)
	{
		require(table, "captured target needs its authored replacement table");
		require(rules["creatureLineSpecialtyTarget"].isString()
			&& rules["creatureLineSpecialtyTarget"].String() == "new-horizons:wisp", "captured Wisp target");
	}
}

std::optional<CreatureID> defaultCreatureLineTarget(const JsonNode & rules, HeroTypeID hero)
{
	validateDefaultCreatureLineSerialization(rules, true);
	if(!rules.isStruct() || !rules.Struct().contains("defaultCreatureLineReplacements"))
		return std::nullopt;
	require(creatureLineSpecialtyRules(rules).has_value(), "default successor needs creature-line coefficients");
	const auto & table = rules["defaultCreatureLineReplacements"].Struct();
	const auto found = table.find(HeroTypeID::encode(hero.getNum()));
	if(found == table.end())
		return std::nullopt;
	return CreatureID(resolve(CreatureID::entityType(), found->second.String()));
}

void validateRemainingStartSerialization(const JsonNode & rules, bool supported)
{
	const auto & specialties = rules["nonDamageSpellSpecialties"];
	if(!supported && specialties.isStruct() && specialties.Struct().contains("remainingStartReplacements"))
		throw std::runtime_error("Remaining starting inscriptions require the new save format");
}

std::optional<NonDamageSpellSpecialtyRules> nonDamageSpellSpecialtyRules(const JsonNode & resolvedRules)
{
	if(!usesRules(resolvedRules) || !resolvedRules.isStruct())
		return std::nullopt;
	const auto found = resolvedRules.Struct().find("nonDamageSpellSpecialties");
	if(found == resolvedRules.Struct().end())
		return std::nullopt;
	validateNonDamageSpellSpecialtyRules(found->second);
	NonDamageSpellSpecialtyRules result;
	result.version = static_cast<int>(found->second["version"].Integer());
	result.componentPercent = static_cast<int>(found->second["componentPercent"].Integer());
	for(const auto & spell : found->second["spells"].Vector())
		result.spells.emplace_back(resolve("spell", spell.String()));
	return result;
}

std::vector<SkillGrowthChance> skillGrowthChances(const JsonNode & resolvedRules,
	const std::function<int(SecondarySkill)> & rank)
{
	std::vector<SkillGrowthChance> result;
	if(!usesRules(resolvedRules)
		|| parsePrimaryProfile(resolvedRules["profile"]).progressionVersion != PRIMARY_PROFILE_VERSION_TWENTY_POINT)
		return result;
	for(const auto & extra : resolvedRules["extraGrowth"].Vector())
	{
		const SecondarySkill skill(resolve(SecondarySkill::entityType(), extra["skill"].String()));
		const int skillRank = rank(skill);
		if(skillRank < 0 || skillRank > 3)
			throw std::runtime_error("Invalid skill rank for primary growth");
		const int chance = extra["chances"].Vector().at(skillRank).Integer();
		if(chance > 0)
			result.push_back({skill, PrimarySkill(extra["primary"].Integer()), chance});
	}
	return result;
}

}
