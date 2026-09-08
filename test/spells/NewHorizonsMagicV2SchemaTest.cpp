/*
 * NewHorizonsMagicV2SchemaTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMagicProfileFixture.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/constants/StringConstants.h"

namespace
{
JsonNode v1Rules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	return rules;
}

JsonNode v2Rules()
{
	auto rules = v1Rules();
	rules["rulesetVersion"].Integer() = 2;
	// Synthetic schema row, not a registered/activated or castable spell.
	auto & spell = rules["spells"]["new-horizons:magicMissile"];
	spell["schools"].Vector().push_back(JsonNode("new-horizons:sorcery"));
	spell["level"].Integer() = 1;
	for(int rank = 0; rank < 4; ++rank)
	{
		JsonNode cost;
		cost.Integer() = 5;
		spell["costs"].Vector().push_back(cost);
	}
	spell["directDamage"]["base"].Integer() = 20;
	spell["directDamage"]["powerCoefficient"].Integer() = 20;
	return rules;
}

bool named(const JsonNode & rules, const std::string & schema)
{
	return JsonUtils::validate(rules, schema, "native named magic v2 schema proof");
}

bool v2(const JsonNode & rules)
{
	return named(rules, "vcmi:newHorizonsMagicV2");
}
}

TEST(NewHorizonsMagicV2SchemaTest, ActualNamedVersionsRemainSeparate)
{
	const auto old = v1Rules();
	ASSERT_TRUE(named(old, "vcmi:newHorizonsMagic"));
	EXPECT_FALSE(v2(old));
	const auto current = v2Rules();
	EXPECT_TRUE(v2(current));
	EXPECT_FALSE(named(current, "vcmi:newHorizonsMagic"));
	auto forbidden = current;
	forbidden["rulesetVersion"].Integer() = 1;
	EXPECT_FALSE(named(forbidden, "vcmi:newHorizonsMagic")) << "V1 still rejects directDamage";
	auto optional = current;
	optional["spells"]["new-horizons:magicMissile"].Struct().erase("directDamage");
	EXPECT_TRUE(v2(optional)) << "Required Missile formula belongs to future data/runtime contract";
}

TEST(NewHorizonsMagicV2SchemaTest, FormulaObjectRejectsNullMissingAndExtraFields)
{
	for(const std::string key : {"base", "powerCoefficient"})
	{
		auto rules = v2Rules();
		rules["spells"]["new-horizons:magicMissile"]["directDamage"].Struct().erase(key);
		EXPECT_FALSE(v2(rules));
	}
	auto rules = v2Rules();
	rules["spells"]["new-horizons:magicMissile"]["directDamage"] = JsonNode();
	EXPECT_FALSE(v2(rules)) << "Native type checking alone permits null; the explicit exclusion is required";
	rules = v2Rules();
	rules["spells"]["new-horizons:magicMissile"]["directDamage"]["divisor"].Integer() = 10;
	EXPECT_FALSE(v2(rules));
}

TEST(NewHorizonsMagicV2SchemaTest, FormulaParametersRequireIntegerRepresentationAndBounds)
{
	for(const std::string key : {"base", "powerCoefficient"})
	{
		for(const int value : {0, 1000000})
		{
			auto rules = v2Rules();
			rules["spells"]["new-horizons:magicMissile"]["directDamage"][key].Integer() = value;
			EXPECT_TRUE(v2(rules));
		}
		for(const int value : {-1, 1000001})
		{
			auto rules = v2Rules();
			rules["spells"]["new-horizons:magicMissile"]["directDamage"][key].Integer() = value;
			EXPECT_FALSE(v2(rules));
		}
		for(const double value : {0.5, 20.0})
		{
			auto rules = v2Rules();
			rules["spells"]["new-horizons:magicMissile"]["directDamage"][key].Float() = value;
			EXPECT_FALSE(v2(rules));
		}
		auto rules = v2Rules();
		rules["spells"]["new-horizons:magicMissile"]["directDamage"][key] = JsonNode();
		EXPECT_FALSE(v2(rules));
		rules["spells"]["new-horizons:magicMissile"]["directDamage"][key].Bool() = true;
		EXPECT_FALSE(v2(rules));
	}
}

TEST(NewHorizonsMagicV2SchemaTest, RealCrossSchemaSchoolAndFactionReferencesRejectInvalidValues)
{
	auto rules = v2Rules();
	rules["spells"]["new-horizons:magicMissile"]["schools"].Vector().front().String() = "core:air";
	EXPECT_FALSE(v2(rules));
	rules = v2Rules();
	rules["schools"].Vector().front().String() = "core:air";
	EXPECT_FALSE(v2(rules));
	rules = v2Rules();
	rules["factions"]["core:castle"]["major"].String() = "core:air";
	EXPECT_FALSE(v2(rules));
}

TEST(NewHorizonsMagicV2SchemaTest, CurrentRealSettingsWrapperHasNotBeenSilentlyUpgraded)
{
	JsonNode settings;
	settings["magic"]["newHorizons"] = v1Rules();
	settings.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	EXPECT_TRUE(named(settings, "vcmi:gameSettings"));
	settings["magic"]["newHorizons"] = v2Rules();
	// Only the explicitly opted-in private profile admits v2 through this
	// wrapper. The ordinary/default wrapper must remain strictly v1.
	EXPECT_EQ(named(settings, "vcmi:gameSettings"), newHorizonsTest::managedMissileProfileRequested());
}
