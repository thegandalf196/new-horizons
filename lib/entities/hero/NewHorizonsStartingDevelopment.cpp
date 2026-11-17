/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsPerkState.h"
#include "NewHorizonsRuleValidation.h"
#include "NewHorizonsStartingPerks.h"
#include "CHeroClass.h"
#include "CHeroHandler.h"
#include "../../spells/CSpellHandler.h"
#include "../../spells/CSpell.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsSpellAvailability.h"

namespace newHorizonsHeroes
{
using detail::fields;
using detail::integer;
using detail::require;
using detail::resolve;
using detail::validateStartingSkills;
namespace
{
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

void validateStartingSkillsImpl(const JsonNode & startingSkills, bool requireMigrationTable)
{
	if(startingSkills.isNull())
		return;

	fields(startingSkills, {"factionSkills", "legacyAliases", "legacySkillMigrations", "magic", "might", "startingDevelopmentProfiles", "startingBookReplacements", "startingPerksPolicyVersion", "startingPerkProfiles"});
	validateStartingPerkProfiles(startingSkills);
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
			if(usesSkillsOnlyStartingProfiles(startingSkills)) fields(profile, {"skills"});
			else fields(profile, {"skills", "startingPerks"});
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
				if(usesBasicStartingProfiles(startingSkills))
					require(skill["rank"].Integer() == MasteryLevel::BASIC, "fresh default Basic starting skill");
			}
			if(usesSkillsOnlyStartingProfiles(startingSkills)) continue;
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

}

namespace detail
{
void validateStartingSkills(const JsonNode & starts, bool requireMigrationTable)
{
	validateStartingSkillsImpl(starts, requireMigrationTable);
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
	if(!usesBasicStartingProfiles(starts)
		&& (HeroTypeID::encode(hero.getNum()) == "core:sylvia" || HeroTypeID::encode(hero.getNum()) == "core:voy")
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
	if(usesSkillsOnlyStartingProfiles(starts)) return result;
	// The generic starting perk already requires catalog admission. Reuse that
	// operation-local validation for both v2 parents rather than rescanning the
	// complete captured registry for each definition.
	const ValidatedPerkRules perkRules(perkState.getRules());
	if(usesBasicStartingProfiles(starts))
	{
		const auto & choices = starts["startingPerkProfiles"][found->first].Vector();
		for(const auto & choice : choices)
		{
			const auto selected = perkRules.definition(choice["skill"].String(), choice["perk"].String());
			require(selected && selected->requiredRank == "basic" && selected->effect["status"].String() == "active",
				"active Basic perk for each fresh default parent");
		}
		const auto & generic = found->second["startingPerks"].Vector().front();
		require(std::ranges::any_of(choices, [&](const JsonNode & choice)
			{ return choice == generic; }), "consistent fresh default generic perk");
	}
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
		const auto selected = perkRules.definition(parent, perk);
		require(installed != result.skills.end() && parentID != *ownFaction && selected
			&& selected->requiredRank == "basic" && selected->effect["status"].String() == "active",
			"active Basic starting perk of installed generic parent");
		proposed.select(parent, perk, installed->second); // Validate tier/cap/duplicate invariants on a copy.
		result.perks.push_back({parent, perk});
	}
	return result;
}

}
