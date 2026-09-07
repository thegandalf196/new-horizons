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
