/*
 * NewHorizonsMagicSchemaTest.cpp, part of VCMI engine
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
JsonNode fullRules()
{
	JsonNode result(JsonPath::builtin("config/newHorizonsMagic"));
	result.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	return result;
}

bool validNamed(const JsonNode & rules)
{
	return JsonUtils::validate(rules, "vcmi:newHorizonsMagic", "native NH schema proof");
}
}

TEST(NewHorizonsMagicSchemaTest, NamedFullEmptyAndRealSettingsWrapperValidate)
{
	const auto full = fullRules();
	ASSERT_TRUE(validNamed(full));
	const JsonNode empty(JsonMap{});
	EXPECT_TRUE(validNamed(empty));
	JsonNode settings;
	settings["magic"]["newHorizons"] = full;
	settings.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	EXPECT_TRUE(JsonUtils::validate(settings, "vcmi:gameSettings", "native real settings wrapper"));
}

TEST(NewHorizonsMagicSchemaTest, NamedSchemaRejectsMalformedNestedSpells)
{
	auto rules = fullRules();
	rules["spells"]["core:implosion"]["level"].Integer() = 0;
	EXPECT_FALSE(validNamed(rules));
	rules = fullRules();
	rules["spells"]["core:implosion"]["schools"].String() = "new-horizons:havoc";
	EXPECT_FALSE(validNamed(rules));
	rules = fullRules();
	rules["spells"]["core:implosion"]["costs"].Vector().clear();
	EXPECT_FALSE(validNamed(rules));
}

TEST(NewHorizonsMagicSchemaTest, NamedSchemaRejectsMalformedNestedFactions)
{
	auto rules = fullRules();
	rules["factions"]["core:castle"]["major"].String() = "core:air";
	EXPECT_FALSE(validNamed(rules));
	rules = fullRules();
	rules["factions"]["core:castle"].Struct().erase("major");
	EXPECT_FALSE(validNamed(rules));
	rules = fullRules();
	rules["factions"]["core:castle"]["provisional"].String() = "yes";
	EXPECT_FALSE(validNamed(rules));
}

TEST(NewHorizonsMagicSchemaTest, NamedSchemaRejectsUnsupportedVersions)
{
	auto rules = fullRules();
	rules["schemaVersion"].Integer() = 2;
	EXPECT_FALSE(validNamed(rules));
	rules = fullRules();
	rules["rulesetVersion"].Integer() = 2;
	EXPECT_FALSE(validNamed(rules));
}
