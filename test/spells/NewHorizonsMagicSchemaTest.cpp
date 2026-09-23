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
JsonNode fullV1Rules()
{
	JsonNode result(JsonPath::builtin("config/newHorizonsMagic"));
	result["rulesetVersion"].Integer() = 1;
	result.Struct().erase("warcasting");
	for(auto & [spellId, spell] : result["spells"].Struct())
	{
		(void)spellId;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
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
	const auto full = fullV1Rules();
	EXPECT_EQ(full["schemaVersion"].Integer(), 1);
	EXPECT_EQ(full["rulesetVersion"].Integer(), 1);
	EXPECT_FALSE(full.Struct().contains("warcasting"));
	for(const auto & [spellId, spell] : full["spells"].Struct())
	{
		(void)spellId;
		EXPECT_FALSE(spell.Struct().contains("active"));
		EXPECT_FALSE(spell.Struct().contains("directDamage"));
		EXPECT_FALSE(spell.Struct().contains("cureAfflictions"));
	}
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
	auto rules = fullV1Rules();
	rules["spells"]["core:implosion"]["level"].Integer() = 0;
	EXPECT_FALSE(validNamed(rules));
	rules = fullV1Rules();
	rules["spells"]["core:implosion"]["schools"].String() = "new-horizons:havoc";
	EXPECT_FALSE(validNamed(rules));
	rules = fullV1Rules();
	rules["spells"]["core:implosion"]["costs"].Vector().clear();
	EXPECT_FALSE(validNamed(rules));
}

TEST(NewHorizonsMagicSchemaTest, NamedSchemaRejectsMalformedNestedFactions)
{
	auto rules = fullV1Rules();
	rules["factions"]["core:castle"]["major"].String() = "core:air";
	EXPECT_FALSE(validNamed(rules));
	rules = fullV1Rules();
	rules["factions"]["core:castle"].Struct().erase("major");
	EXPECT_FALSE(validNamed(rules));
	rules = fullV1Rules();
	rules["factions"]["core:castle"]["provisional"].String() = "yes";
	EXPECT_FALSE(validNamed(rules));
}

TEST(NewHorizonsMagicSchemaTest, NamedSchemaRejectsUnsupportedVersions)
{
	auto rules = fullV1Rules();
	rules["schemaVersion"].Integer() = 2;
	EXPECT_FALSE(validNamed(rules));
	rules = fullV1Rules();
	rules["rulesetVersion"].Integer() = 2;
	EXPECT_FALSE(validNamed(rules));
}
