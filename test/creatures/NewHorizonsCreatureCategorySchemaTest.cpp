/*
 * NewHorizonsCreatureCategorySchemaTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/constants/StringConstants.h"

#include <map>
#include <string>
#include <string_view>

namespace
{
JsonNode configuredCategories()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsCreatureCategories"));
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	return rules;
}

bool validCategories(const JsonNode & rules)
{
	return JsonUtils::validate(rules, "vcmi:newHorizonsCreatureCategories", "native category schema proof");
}

bool validCategorySettings(const JsonNode & rules)
{
	JsonNode settings;
	settings["creatures"]["newHorizonsCategories"] = rules;
	settings.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	return JsonUtils::validate(settings, "vcmi:gameSettings", "native category settings wrapper proof");
}

void addCanonicalLines(std::map<std::string, std::string> & assignments, std::string_view category,
	std::initializer_list<std::string_view> lines)
{
	for(const auto line : lines)
		assignments.emplace("core:" + std::string(line), category);
}

std::map<std::string, std::string> canonicalAssignments()
{
	std::map<std::string, std::string> assignments;
	addCanonicalLines(assignments, "core", {"pikeman", "halberdier", "archer", "marksman", "swordsman", "crusader"});
	addCanonicalLines(assignments, "elite", {"griffin", "royalGriffin", "monk", "zealot", "cavalier", "champion"});
	addCanonicalLines(assignments, "champion", {"angel", "archangel"});

	addCanonicalLines(assignments, "core", {"centaur", "centaurCaptain", "dwarf", "battleDwarf", "woodElf", "grandElf"});
	addCanonicalLines(assignments, "elite", {"pegasus", "silverPegasus", "dendroidGuard", "dendroidSoldier", "unicorn", "warUnicorn"});
	addCanonicalLines(assignments, "champion", {"greenDragon", "goldDragon"});

	addCanonicalLines(assignments, "core", {"gremlin", "masterGremlin", "stoneGargoyle", "obsidianGargoyle", "ironGolem", "stoneGolem"});
	addCanonicalLines(assignments, "elite", {"mage", "archMage", "genie", "masterGenie", "naga", "nagaQueen"});
	addCanonicalLines(assignments, "champion", {"giant", "titan"});

	addCanonicalLines(assignments, "core", {"imp", "familiar", "gog", "magog", "hellHound", "cerberus"});
	addCanonicalLines(assignments, "elite", {"demon", "hornedDemon", "pitFiend", "pitLord", "efreet", "efreetSultan"});
	addCanonicalLines(assignments, "champion", {"devil", "archDevil"});

	addCanonicalLines(assignments, "core", {"skeleton", "skeletonWarrior", "walkingDead", "zombieLord", "wight", "wraith"});
	addCanonicalLines(assignments, "elite", {"vampire", "vampireLord", "lich", "powerLich", "blackKnight", "dreadKnight"});
	addCanonicalLines(assignments, "champion", {"boneDragon", "ghostDragon"});

	addCanonicalLines(assignments, "core", {"troglodyte", "infernalTroglodyte", "harpy", "harpyHag", "beholder", "evilEye"});
	addCanonicalLines(assignments, "elite", {"medusa", "medusaQueen", "minotaur", "minotaurKing", "manticore", "scorpicore"});
	addCanonicalLines(assignments, "champion", {"redDragon", "blackDragon"});

	addCanonicalLines(assignments, "core", {"goblin", "hobgoblin", "goblinWolfRider", "hobgoblinWolfRider", "orc", "orcChieftain"});
	addCanonicalLines(assignments, "elite", {"ogre", "ogreMage", "roc", "thunderbird", "cyclop", "cyclopKing"});
	addCanonicalLines(assignments, "champion", {"behemoth", "ancientBehemoth"});

	addCanonicalLines(assignments, "core", {"gnoll", "gnollMarauder", "lizardman", "lizardWarrior", "serpentFly", "fireDragonFly"});
	addCanonicalLines(assignments, "elite", {"basilisk", "greaterBasilisk", "gorgon", "mightyGorgon", "wyvern", "wyvernMonarch"});
	addCanonicalLines(assignments, "champion", {"hydra", "chaosHydra"});

	// Conflux has two independent Core lines, five Elite elemental lines, and one
	// Champion line; upgrades remain explicit rows even when a line is independent.
	addCanonicalLines(assignments, "core", {"pixie", "sprite"});
	addCanonicalLines(assignments, "elite", {"airElemental", "stormElemental", "waterElemental", "iceElemental", "fireElemental", "energyElemental",
		"earthElemental", "magmaElemental", "psychicElemental", "magicElemental"});
	addCanonicalLines(assignments, "champion", {"firebird", "phoenix"});
	return assignments;
}
}

TEST(NewHorizonsCreatureCategorySchemaTest, ActualNamedConfigAndRealSettingsWrapperValidate)
{
	const auto rules = configuredCategories();
	ASSERT_TRUE(rules["creatures"].isStruct());
	ASSERT_FALSE(rules["creatures"].Struct().empty());
	EXPECT_TRUE(validCategories(rules));
	EXPECT_TRUE(validCategorySettings(rules));
	const JsonNode absent(JsonMap{});
	EXPECT_TRUE(validCategories(absent));
	EXPECT_TRUE(validCategorySettings(absent));
}

TEST(NewHorizonsCreatureCategorySchemaTest, CanonicalRosterCoversEveryFactionLineAndUpgrade)
{
	const auto rules = configuredCategories();
	const auto expected = canonicalAssignments();
	const auto & rows = rules["creatures"].Struct();
	ASSERT_EQ(rows.size(), expected.size()) << "Canonical New Horizons data must not silently omit a roster row";

	std::map<std::string, size_t> categoryCounts;
	for(const auto & [key, category] : expected)
	{
		const auto found = rows.find(key);
		ASSERT_NE(found, rows.end()) << "Missing canonical creature assignment: " << key;
		ASSERT_TRUE(found->second.isString()) << "Canonical assignment is not a string: " << key;
		EXPECT_EQ(found->second.String(), category) << "Wrong canonical category for " << key;
		++categoryCounts[category];
	}

	for(const auto & [key, value] : rows)
		EXPECT_TRUE(expected.contains(key)) << "Unexpected creature assignment: " << key;
	EXPECT_EQ(categoryCounts["core"], 50u);
	EXPECT_EQ(categoryCounts["elite"], 58u);
	EXPECT_EQ(categoryCounts["champion"], 18u);
}

TEST(NewHorizonsCreatureCategorySchemaTest, RejectsUnsupportedVersionsAndUnknownRootFields)
{
	auto rules = configuredCategories();
	rules["schemaVersion"].Integer() = 2;
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules["rulesetVersion"].Float() = 1.5;
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules.Struct().erase("sourceRulesetId");
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules["legacyTier"].Integer() = 1;
	EXPECT_FALSE(validCategories(rules));
}

TEST(NewHorizonsCreatureCategorySchemaTest, RequiresAllThreeDefinitionsAndNonemptyTypedTexts)
{
	auto rules = configuredCategories();
	rules["categories"].Struct().erase("champion");
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules["categories"]["core"]["nameTextId"].String().clear();
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules["categories"]["elite"]["descriptionTextId"].Integer() = 1;
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules["categories"]["champion"]["attackBonus"].Integer() = 10;
	EXPECT_FALSE(validCategories(rules));
}

TEST(NewHorizonsCreatureCategorySchemaTest, TypedAdditionalCreaturePropertiesRejectNumericAndUnknownCategories)
{
	auto rules = configuredCategories();
	rules["creatures"]["core:pixie"].Integer() = 1;
	EXPECT_FALSE(validCategories(rules));
	EXPECT_FALSE(validCategorySettings(rules)) << "Real settings must reference the named schema";
	rules = configuredCategories();
	rules["creatures"]["core:pixie"].String() = "tier1";
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules["creatures"].Struct().clear();
	EXPECT_FALSE(validCategories(rules));
	rules = configuredCategories();
	rules.Struct().erase("creatures");
	EXPECT_FALSE(validCategories(rules));
}

TEST(NewHorizonsCreatureCategorySchemaTest, SchemaShapeDoesNotClaimCanonicalEntityResolution)
{
	auto rules = configuredCategories();
	rules["creatures"]["core:missingCategoryCreature"].String() = "core";
	// The schema verifies typed rows. Runtime capture independently rejects this
	// unknown entity before publishing any row; see the actual world-state test.
	EXPECT_TRUE(validCategories(rules));
}
