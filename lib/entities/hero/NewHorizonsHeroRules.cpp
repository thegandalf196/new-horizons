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

void validateSkillSpecialtyRules(const JsonNode & rules)
{
	fields(rules, {"version", "coreBonusPercent", "skills", "navigationStartReplacements"});
	if(rules.Struct().contains("navigationStartReplacements"))
		require(rules["navigationStartReplacements"].isBool(), "navigation starting replacement Boolean");
	require(integer(rules["version"], 1, 1), "skill specialty version");
	require(integer(rules["coreBonusPercent"], 20, 20), "skill specialty core bonus percentage");
	const auto & skills = rules["skills"];
	require(skills.isVector() && !skills.Vector().empty() && skills.Vector().size() <= 5,
		"version 1 skill specialties must list one to five supported core skills");
	std::set<int> seen;
	for(const auto & skill : skills.Vector())
	{
		require(skill.isString()
			&& (skill.String() == "core:logistics" || skill.String() == "core:armorer"
				|| skill.String() == "core:offence" || skill.String() == "core:archery"
				|| skill.String() == "core:estates"),
			"version 1 skill specialties support only core:logistics, core:armorer, core:offence, core:archery and core:estates");
		const int skillId = resolve(SecondarySkill::entityType(), skill.String());
		require(skillId == SecondarySkill::LOGISTICS || skillId == SecondarySkill::ARMORER
			|| skillId == SecondarySkill::OFFENCE || skillId == SecondarySkill::ARCHERY
			|| skillId == SecondarySkill::ESTATES,
			"unknown version 1 skill specialty");
		require(seen.insert(skillId).second, "duplicate version 1 skill specialty");
	}
	if(rules.Struct().contains("navigationStartReplacements") && rules["navigationStartReplacements"].Bool())
		require(seen.contains(SecondarySkill::LOGISTICS), "Navigation successor requires the Logistics core specialty");
}

void validateOptionalSkillSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct())
		return;
	const auto found = rules.Struct().find("skillSpecialties");
	if(found != rules.Struct().end())
		validateSkillSpecialtyRules(found->second);
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

struct StartingBookRow
{
	std::string_view hero;
	std::string_view from;
	std::string_view to;
};
constexpr std::array<StartingBookRow, 22> startingBooks{{
	{"core:rion", "core:stoneSkin", "new-horizons:guardianSpirit"},
	{"core:aeris", "core:protectAir", "new-horizons:holyArmor"},
	{"core:piquedram", "core:shield", "core:slow"},
	{"core:neela", "core:shield", "core:slow"},
	{"core:theodorus", "core:shield", "core:slow"},
	{"core:ayden", "core:viewEarth", "new-horizons:confusion"},
	{"core:axsis", "core:protectAir", "core:forgetfulness"},
	{"core:zydar", "core:stoneSkin", "new-horizons:blink"},
	{"core:vokial", "core:stoneSkin", "new-horizons:lifeDrain"},
	{"core:galthran", "core:shield", "core:slow"},
	{"core:nimbus", "core:shield", "core:dispel"},
	{"core:nagash", "core:protectAir", "core:dispel"},
	{"core:jaegar", "core:shield", "core:curse"},
	{"core:malekith", "core:bloodlust", "new-horizons:shadowGift"},
	{"core:sephinroth", "core:protectAir", "core:curse"},
	{"core:gird", "core:bloodlust", "new-horizons:vengefulVines"},
	{"core:dessa", "core:stoneSkin", "new-horizons:regeneration"},
	{"core:oris", "core:protectAir", "core:forgetfulness"},
	{"core:saurug", "core:bloodlust", "new-horizons:vengefulVines"},
	{"core:verdish", "core:protectFire", "new-horizons:regeneration"},
	{"core:styg", "core:shield", "new-horizons:entangle"},
	{"core:tiva", "core:stoneSkin", "new-horizons:regeneration"}
}};

void validateStartingBooks(const JsonNode & table)
{
	require(table.isStruct() && table.Struct().size() == startingBooks.size(), "complete starting book replacement table");
	std::set<int> seen;
	for(const auto & [hero, row] : table.Struct())
	{
		const auto expected = std::find_if(startingBooks.begin(), startingBooks.end(),
			[&hero](const StartingBookRow & entry) { return entry.hero == hero; });
		require(expected != startingBooks.end(), "authored starting book hero");
		const int id = resolve(HeroTypeID::entityType(), hero);
		require(HeroTypeID::encode(id) == hero && seen.insert(id).second
			&& static_cast<size_t>(id) < LIBRARY->heroh->objects.size()
			&& LIBRARY->heroh->objects[id] && !LIBRARY->heroh->objects[id]->special,
			"installed unique standard starting book hero");
		fields(row, {"from", "to"});
		require(row["from"].isString() && row["to"].isString()
			&& row["from"].String() == expected->from && row["to"].String() == expected->to,
			"exact authored starting book successor");
		const SpellID from(resolve(SpellID::entityType(), row["from"].String()));
		const SpellID to(resolve(SpellID::entityType(), row["to"].String()));
		require(LIBRARY->heroh->objects[id]->spells.contains(from), "exact prototype starting inscription");
		require(static_cast<size_t>(to.getNum()) < LIBRARY->spellh->objects.size()
			&& LIBRARY->spellh->objects[to.getNum()]
			&& to.toSpell()->isCommonHeroSpell() && to.toSpell()->isCombat(),
			"installed ordinary combat starting successor");
	}
}

void validateStartingSkills(const JsonNode & startingSkills, bool requireMigrationTable)
{
	if(startingSkills.isNull())
		return;

	fields(startingSkills, {"factionSkills", "legacyAliases", "legacySkillMigrations", "magic", "might", "startingDevelopmentProfiles", "startingBookReplacements"});
	if(startingSkills.Struct().contains("startingBookReplacements"))
		validateStartingBooks(startingSkills["startingBookReplacements"]);
	if(startingSkills.Struct().contains("startingDevelopmentProfiles"))
	{
		const auto & profiles = startingSkills["startingDevelopmentProfiles"];
		require(profiles.isStruct(), "starting development profiles object");
		std::set<int> heroes;
		for(const auto & [hero, profile] : profiles.Struct())
		{
			const auto id = resolve(HeroTypeID::entityType(), hero);
			require(HeroTypeID::encode(id) == hero && heroes.insert(id).second, "canonical unique starting hero");
			fields(profile, {"skills", "startingPerks"});
			require(profile["skills"].isVector() && profile["skills"].Vector().size() == 2,
				"exactly two explicit starting skills");
			std::set<int> skills;
			for(const auto & skill : profile["skills"].Vector())
			{
				fields(skill, {"skill", "rank"});
				require(skill["skill"].isString(), "starting skill identifier");
				const auto name = skill["skill"].String();
				const auto skillID = resolve(SecondarySkill::entityType(), name);
				require(SecondarySkill::encode(skillID) == name && name.starts_with("new-horizons:")
					&& skills.insert(skillID).second, "canonical unique New Horizons starting skill");
				require(integer(skill["rank"], MasteryLevel::BASIC, MasteryLevel::EXPERT), "starting skill rank");
			}
			require(profile["startingPerks"].isVector() && profile["startingPerks"].Vector().size() == 1,
				"one explicit starting Basic perk");
			const auto & perk = profile["startingPerks"].Vector().front();
			fields(perk, {"skill", "perk"});
			require(perk["skill"].isString() && perk["perk"].isString(), "starting perk identifiers");
			const auto parent = resolve(SecondarySkill::entityType(), perk["skill"].String());
			require(skills.contains(parent) && perk["perk"].String().starts_with(perk["skill"].String() + "."),
				"starting perk parent installed");
		}
	}
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
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "classProfiles", "skillOfferWeights", "excludedSkills", "extraGrowth", "startingSkills", "creatureLineSpecialties", "damageSpellSpecialties", "nonDamageSpellSpecialties", "skillSpecialties", "remainingSpellSpecialtyReplacements", "lighthouseDeparture", "defaultCreatureLineReplacements"});
	newHorizonsLighthouse::validateRulesSerialization(rules, true);
	validateCommon(rules);
	validateOptionalCreatureLineSpecialtyRules(rules);
	validateOptionalDamageSpellSpecialtyRules(rules);
	validateOptionalNonDamageSpellSpecialtyRules(rules);
	validateOptionalSkillSpecialtyRules(rules);
	validateRemainingSpellSpecialtySerialization(rules, true);
	validateDefaultCreatureLineSerialization(rules, true);
	if(rules.Struct().contains("defaultCreatureLineReplacements"))
		require(creatureLineSpecialtyRules(rules).has_value(), "default successor needs creature-line coefficients");
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
	fields(rules, {"schemaVersion", "rulesetVersion", "powerDivisor", "maxPrimary", "profile", "skillOfferWeights", "excludedSkills", "extraGrowth", "startingSkills", "creatureLineSpecialties", "damageSpellSpecialties", "nonDamageSpellSpecialties", "skillSpecialties", "remainingSpellSpecialtyReplacements", "lighthouseDeparture", "defaultCreatureLineReplacements", "creatureLineSpecialtyTarget"});
	newHorizonsLighthouse::validateRulesSerialization(rules, true);
	validateCommon(rules);
	validateOptionalCreatureLineSpecialtyRules(rules);
	validateOptionalDamageSpellSpecialtyRules(rules);
	validateOptionalNonDamageSpellSpecialtyRules(rules);
	validateOptionalSkillSpecialtyRules(rules);
	validateRemainingSpellSpecialtySerialization(rules, true);
	validateDefaultCreatureLineSerialization(rules, true);
	if(rules.Struct().contains("defaultCreatureLineReplacements"))
		require(creatureLineSpecialtyRules(rules).has_value(), "default successor needs creature-line coefficients");
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
	if(rules.Struct().contains("lighthouseDeparture"))
		result["lighthouseDeparture"] = rules["lighthouseDeparture"];
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

void validateStartingBookSerialization(const JsonNode & rules, bool supported)
{
	const auto & starts = rules["startingSkills"];
	if(!supported && starts.isStruct() && starts.Struct().contains("startingBookReplacements"))
		throw std::runtime_error("Starting book replacements require the new save format");
	if(starts.isStruct() && starts.Struct().contains("startingBookReplacements"))
		validateStartingBooks(starts["startingBookReplacements"]);
}

std::optional<SpellID> startingBookReplacement(
	const JsonNode & rules, const JsonNode & magicRules, HeroTypeID hero, SpellID original)
{
	if(!usesRules(rules) || !rules["startingSkills"].isStruct()
		|| !rules["startingSkills"].Struct().contains("startingBookReplacements"))
		return std::nullopt;
	const auto & table = rules["startingSkills"]["startingBookReplacements"];
	validateStartingBooks(table);
	require(newHorizonsMagic::rulesActive(magicRules), "starting books require current magic roster");
	// Validate every row against the captured world, not mutable module defaults.
	for(const auto & [key, row] : table.Struct())
	{
		const auto * prototype = HeroTypeID(resolve(HeroTypeID::entityType(), key)).toHeroType();
		const SpellID target(resolve(SpellID::entityType(), row["to"].String()));
		require(newHorizonsMagic::spellAllowedByHeroRoster(magicRules, target)
			&& newHorizonsMagic::spellAvailableForOrdinaryAcquisition(magicRules, target),
			"ordinary captured starting spell roster");
		const auto preferred = newHorizonsMagic::preferredSchools(magicRules, prototype->heroClass->faction);
		const auto schools = newHorizonsMagic::spellSchools(magicRules, target);
		require(std::any_of(schools.begin(), schools.end(),
			[&preferred](SpellSchool school) { return vstd::contains(preferred, school); }),
			"preferred starting spell school");
	}
	if(hero == HeroTypeID::NONE)
		return std::nullopt;
	const auto found = table.Struct().find(HeroTypeID::encode(hero.getNum()));
	if(found == table.Struct().end() || SpellID::decode(found->second["from"].String()) != original.getNum())
		return std::nullopt;
	return SpellID(resolve(SpellID::entityType(), found->second["to"].String()));
}

void validateStartingDevelopmentSerialization(const JsonNode & rules, bool supported)
{
	const auto & starts = rules["startingSkills"];
	if(!supported && starts.isStruct() && starts.Struct().contains("startingDevelopmentProfiles"))
		throw std::runtime_error("Starting development profiles require the new save format");
}

void validateCoroniusHolyWrathSerialization(const JsonNode & rules, bool supported)
{
	const auto & specialties = rules["damageSpellSpecialties"];
	if(!supported && specialties.isStruct() && specialties.Struct().contains("coroniusHolyWrathReplacement"))
		throw std::runtime_error("Coronius Holy Wrath replacement rules require the new save format");
}

bool usesNavigationStartReplacement(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules["skillSpecialties"].isStruct())
		return false;
	const auto & specialties = rules["skillSpecialties"];
	validateSkillSpecialtyRules(specialties);
	return specialties.Struct().contains("navigationStartReplacements")
		&& specialties["navigationStartReplacements"].Bool();
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

void validateNavigationStartSerialization(const JsonNode & rules, bool supported)
{
	const auto & specialties = rules["skillSpecialties"];
	if(!supported && specialties.isStruct() && specialties.Struct().contains("navigationStartReplacements"))
		throw std::runtime_error("Navigation successor profile requires the new save format");
}

std::optional<StartingDevelopmentProfile> startingDevelopmentProfile(
	const JsonNode & rules, const PerkState & perkState, HeroTypeID hero, HeroClassID heroClass)
{
	if(!usesRules(rules) || !rules["startingSkills"].isStruct())
		return std::nullopt;
	const auto & starts = rules["startingSkills"];
	if(!starts.Struct().contains("startingDevelopmentProfiles"))
		return std::nullopt;
	validateStartingSkills(starts, false);
	const auto & profiles = starts["startingDevelopmentProfiles"].Struct();
	const auto found = profiles.find(HeroTypeID::encode(hero.getNum()));
	if(found == profiles.end())
		return std::nullopt;
	if((HeroTypeID::encode(hero.getNum()) == "core:sylvia" || HeroTypeID::encode(hero.getNum()) == "core:voy")
		&& !usesNavigationStartReplacement(rules))
		return std::nullopt;
	const auto * definition = heroClass.toHeroClass();
	require(definition != nullptr, "starting hero class");
	const auto ownFaction = factionSkill(rules, definition->faction);
	require(ownFaction.has_value() && usesSkillOfferWeights(rules), "captured starting faction and class weights");
	StartingDevelopmentProfile result;
	bool hasFaction = false;
	for(const auto & entry : found->second["skills"].Vector())
	{
		const SecondarySkill skill(SecondarySkill::decode(entry["skill"].String()));
		const auto rank = static_cast<ui8>(entry["rank"].Integer());
		if(skill == *ownFaction)
		{
			// Explicit captured profiles preserve their authored faction mastery.
			// This does not grant a rank to heroes without a selected profile.
			require(rank >= MasteryLevel::BASIC && rank <= MasteryLevel::EXPERT,
				"authored starting faction rank");
			hasFaction = true;
		}
		else
		{
			require(rank <= MasteryLevel::ADVANCED, "Basic or Advanced generic starting parent");
			const auto weight = skillOfferWeight(rules, skill);
			require(weight && *weight > 0 && !isExcludedSkill(rules, skill)
				&& !isFactionSkill(rules, skill), "class-legal generic starting parent");
		}
		result.skills.emplace_back(skill, rank);
	}
	require(hasFaction, "own starting faction skill");
	auto proposed = perkState;
	for(const auto & entry : found->second["startingPerks"].Vector())
	{
		const auto parent = entry["skill"].String();
		const auto perk = entry["perk"].String();
		const SecondarySkill parentID(SecondarySkill::decode(parent));
		const auto installed = std::ranges::find_if(result.skills, [parentID](const auto & skill)
		{
			return skill.first == parentID;
		});
		const auto selected = perkDefinition(perkState.rules, parent, perk);
		require(installed != result.skills.end() && parentID != *ownFaction && selected
			&& selected->requiredRank == "basic" && selected->effect["status"].String() == "active",
			"active Basic starting perk of installed generic parent");
		proposed.select(parent, perk, installed->second); // Validate tier/cap/duplicate invariants on a copy.
		result.perks.push_back({parent, perk});
	}
	return result;
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

std::optional<SkillSpecialtyRules> skillSpecialtyRules(const JsonNode & resolvedRules)
{
	if(!usesRules(resolvedRules) || !resolvedRules.isStruct())
		return std::nullopt;
	const auto found = resolvedRules.Struct().find("skillSpecialties");
	if(found == resolvedRules.Struct().end())
		return std::nullopt;
	validateSkillSpecialtyRules(found->second);
	SkillSpecialtyRules result;
	result.version = static_cast<int>(found->second["version"].Integer());
	result.coreBonusPercent = static_cast<int>(found->second["coreBonusPercent"].Integer());
	for(const auto & skill : found->second["skills"].Vector())
		result.skills.emplace_back(resolve(SecondarySkill::entityType(), skill.String()));
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
