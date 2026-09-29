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
#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/constants/StringConstants.h"

namespace
{
JsonNode v1Rules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = 1;
	rules.Struct().erase("warcasting");
	rules.Struct().erase("spellPoints");
	rules.Struct().erase("mageGuildGeneration");
	rules.Struct().erase("physicalDamageReductionCapPercent");
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, faction] : rules["factions"].Struct())
	{
		(void)name;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("active");
		spell.Struct().erase("directDamage");
		spell.Struct().erase("cureAfflictions");
	}
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	return rules;
}

JsonNode v2Rules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = 2;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
	return rules;
}

JsonNode v3Rules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules.setModScope(GameConstants::NEW_HORIZONS_MOD_SCOPE);
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

bool v3(const JsonNode & rules)
{
	return named(rules, "vcmi:newHorizonsMagicV3");
}

JsonNode settingsWithCoreScopedMagicRoot(const JsonNode & rules)
{
	JsonNode settings;
	settings["magic"]["newHorizons"] = rules;
	settings.setModScope("core", false);
	settings["magic"].setModScope("core", false);
	settings["magic"]["newHorizons"].setModScope("core", false);
	return settings;
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
	const auto currentV3 = v3Rules();
	EXPECT_TRUE(v3(currentV3));
	EXPECT_FALSE(v2(currentV3));
	auto forbidden = current;
	forbidden["rulesetVersion"].Integer() = 1;
	EXPECT_FALSE(named(forbidden, "vcmi:newHorizonsMagic")) << "V1 still rejects directDamage";
	auto optional = current;
	optional["spells"]["core:magicArrow"].Struct().erase("directDamage");
	EXPECT_TRUE(v2(optional)) << "The generic schema permits formulas only on spells that define them; runtime enforces the Magic Arrow contract";
	EXPECT_FALSE(v3(current)) << "V3 requires its snapshotted school-rank factors";
	EXPECT_FALSE(v2(currentV3)) << "V2 does not accept the v3 ruleset or its new factors";
}

TEST(NewHorizonsMagicV3SchemaTest, SchoolRankFactorsAreRequiredAndValidateAllFourRanks)
{
	auto current = v3Rules();
	ASSERT_TRUE(v3(current));
	const std::array<int, 4> expected{100, 115, 130, 145};
	ASSERT_EQ(current["schoolRankPowerCoefficientPercent"].Vector().size(), expected.size());
	for(size_t rank = 0; rank < expected.size(); ++rank)
	{
		EXPECT_EQ(current["schoolRankPowerCoefficientPercent"].Vector()[rank].Integer(), expected[rank]);
		auto malformed = current;
		malformed["schoolRankPowerCoefficientPercent"].Vector()[rank].Integer() = expected[rank] + 1;
		EXPECT_FALSE(v3(malformed)) << "wrong factor at rank index " << rank;
	}
	auto missing = current;
	missing.Struct().erase("schoolRankPowerCoefficientPercent");
	EXPECT_FALSE(v3(missing));
	auto shortArray = current;
	shortArray["schoolRankPowerCoefficientPercent"].Vector().pop_back();
	EXPECT_FALSE(v3(shortArray));
	auto extra = current;
	extra["schoolRankPowerCoefficientPercent"].Vector().push_back(JsonNode(160));
	EXPECT_FALSE(v3(extra));
}

TEST(NewHorizonsMagicV3SchemaTest, SpellcraftFactorsAreOptionalAndValidateAllFourRanks)
{
	auto current = v3Rules();
	ASSERT_TRUE(v3(current));
	const std::array<int, 4> expected{100, 110, 120, 130};
	ASSERT_EQ(current["spellcraftEfficiencyPercent"].Vector().size(), expected.size());
	for(size_t rank = 0; rank < expected.size(); ++rank)
	{
		EXPECT_EQ(current["spellcraftEfficiencyPercent"].Vector()[rank].Integer(), expected[rank]);
		auto malformed = current;
		malformed["spellcraftEfficiencyPercent"].Vector()[rank].Integer() = expected[rank] + 1;
		EXPECT_FALSE(v3(malformed)) << "wrong Spellcraft factor at rank index " << rank;
	}

	auto oldV3 = current;
	oldV3.Struct().erase("spellcraftEfficiencyPercent");
	EXPECT_TRUE(v3(oldV3)) << "Older v3 snapshots remain valid without the optional Spellcraft contract";
	auto fractional = current;
	fractional["spellcraftEfficiencyPercent"].Vector()[1].Float() = 110.0;
	EXPECT_FALSE(v3(fractional));
	auto shortArray = current;
	shortArray["spellcraftEfficiencyPercent"].Vector().pop_back();
	EXPECT_FALSE(v3(shortArray));
	auto extra = current;
	extra["spellcraftEfficiencyPercent"].Vector().push_back(JsonNode(140));
	EXPECT_FALSE(v3(extra));

	auto v2WithField = v2Rules();
	v2WithField["spellcraftEfficiencyPercent"] = current["spellcraftEfficiencyPercent"];
	EXPECT_FALSE(v2(v2WithField)) << "The v2 schema rejects the v3-only Spellcraft field";
	auto v1WithField = v1Rules();
	v1WithField["spellcraftEfficiencyPercent"] = current["spellcraftEfficiencyPercent"];
	EXPECT_FALSE(named(v1WithField, "vcmi:newHorizonsMagic"));
}

TEST(NewHorizonsMagicV2SchemaTest, FormulaObjectRejectsNullMissingAndExtraFields)
{
	for(const std::string key : {"base", "powerCoefficient"})
	{
		auto rules = v2Rules();
		rules["spells"]["core:magicArrow"]["directDamage"].Struct().erase(key);
		EXPECT_FALSE(v2(rules));
	}
	auto rules = v2Rules();
	rules["spells"]["core:magicArrow"]["directDamage"] = JsonNode();
	EXPECT_FALSE(v2(rules)) << "Native type checking alone permits null; the explicit exclusion is required";
	rules = v2Rules();
	rules["spells"]["core:magicArrow"]["directDamage"]["divisor"].Integer() = 10;
	EXPECT_FALSE(v2(rules));
}

TEST(NewHorizonsMagicV2SchemaTest, ActiveFlagIsOptionalAndBoolean)
{
	auto rules = v2Rules();
	EXPECT_TRUE(v2(rules)) << "Older v2 rows without an active marker remain valid";
	rules["spells"]["core:clone"]["active"].Bool() = false;
	EXPECT_TRUE(v2(rules));
	rules["spells"]["new-horizons:phantomArmy"]["active"].Bool() = true;
	EXPECT_TRUE(v2(rules));
	rules["spells"]["core:clone"]["active"] = JsonNode();
	EXPECT_FALSE(v2(rules));
	rules["spells"]["core:clone"]["active"].String() = "false";
	EXPECT_FALSE(v2(rules));
}

TEST(NewHorizonsMagicV2SchemaTest, WarcastingOptInRequiresBooleanAndAllowsAbsence)
{
	auto rules = v2Rules();
	rules.Struct().erase("warcasting");
	EXPECT_TRUE(v2(rules));
	rules["warcasting"].Bool() = false;
	EXPECT_TRUE(v2(rules));
	rules["warcasting"].Bool() = true;
	EXPECT_TRUE(v2(rules));
	rules["warcasting"].String() = "true";
	EXPECT_FALSE(v2(rules));
	rules["warcasting"].Integer() = 1;
	EXPECT_FALSE(v2(rules));
	rules["warcasting"] = JsonNode();
	EXPECT_FALSE(v2(rules));
}

TEST(NewHorizonsMagicV2SchemaTest, MageGuildGenerationRequiresCanonicalFiveLevelSlotProfile)
{
	auto rules = v2Rules();
	EXPECT_TRUE(v2(rules));
	rules.Struct().erase("mageGuildGeneration");
	EXPECT_TRUE(v2(rules)) << "Older v2 saves keep the historical generation algorithm";

	rules = v2Rules();
	rules["mageGuildGeneration"]["rulesetVersion"].Integer() = 2;
	EXPECT_FALSE(v2(rules));
	rules = v2Rules();
	rules["mageGuildGeneration"]["nonPreferredSlots"].Vector().pop_back();
	EXPECT_FALSE(v2(rules));
	for(const int invalid : {-1, 4, 5})
	{
		rules = v2Rules();
		rules["mageGuildGeneration"]["nonPreferredSlots"].Vector().front().Integer() = invalid;
		EXPECT_FALSE(v2(rules));
	}
	rules = v2Rules();
	rules["mageGuildGeneration"] = JsonNode();
	EXPECT_FALSE(v2(rules));

	rules = v2Rules();
	rules.Struct().erase("factions");
	EXPECT_FALSE(v2(rules));
	rules = v2Rules();
	rules["factions"].Struct().erase("core:castle");
	EXPECT_TRUE(v2(rules)) << "The generic schema permits modded faction sets; runtime enforces core coverage";
	rules = v2Rules();
	rules["factionWeights"]["major"].Integer() = 3;
	rules["factionWeights"]["minor"].Integer() = 1;
	EXPECT_TRUE(v2(rules)) << "Cross-mode compatibility is enforced by newHorizonsMagic::validateRules; required/not is scope-sensitive in merged settings";
}

TEST(NewHorizonsMagicV2SchemaTest, PhysicalReductionCapRequiresIntegerPercentageAndAllowsAbsence)
{
	auto rules = v2Rules();
	rules.Struct().erase("physicalDamageReductionCapPercent");
	EXPECT_TRUE(v2(rules));
	for(const int cap : {0, 80, 100})
	{
		rules["physicalDamageReductionCapPercent"].Integer() = cap;
		EXPECT_TRUE(v2(rules));
	}
	for(const int cap : {-1, 101})
	{
		rules["physicalDamageReductionCapPercent"].Integer() = cap;
		EXPECT_FALSE(v2(rules));
	}
	rules["physicalDamageReductionCapPercent"].Float() = 80.5;
	EXPECT_FALSE(v2(rules));
	rules["physicalDamageReductionCapPercent"] = JsonNode();
	EXPECT_FALSE(v2(rules));
}

TEST(NewHorizonsMagicV2SchemaTest, FormulaParametersRequireIntegerRepresentationAndBounds)
{
	for(const std::string key : {"base", "powerCoefficient"})
	{
		for(const int value : {0, 1000000})
		{
			auto rules = v2Rules();
			rules["spells"]["core:magicArrow"]["directDamage"][key].Integer() = value;
			EXPECT_TRUE(v2(rules));
		}
		for(const int value : {-1, 1000001})
		{
			auto rules = v2Rules();
			rules["spells"]["core:magicArrow"]["directDamage"][key].Integer() = value;
			EXPECT_FALSE(v2(rules));
		}
		for(const double value : {0.5, 20.0})
		{
			auto rules = v2Rules();
			rules["spells"]["core:magicArrow"]["directDamage"][key].Float() = value;
			EXPECT_FALSE(v2(rules));
		}
		auto rules = v2Rules();
		rules["spells"]["core:magicArrow"]["directDamage"][key] = JsonNode();
		EXPECT_FALSE(v2(rules));
		rules["spells"]["core:magicArrow"]["directDamage"][key].Bool() = true;
		EXPECT_FALSE(v2(rules));
	}
}

TEST(NewHorizonsMagicV2SchemaTest, RealCrossSchemaSchoolAndFactionReferencesRejectInvalidValues)
{
	auto rules = v2Rules();
	rules["spells"]["core:magicArrow"]["schools"].Vector().front().String() = "core:air";
	EXPECT_FALSE(v2(rules));
	rules = v2Rules();
	rules["schools"].Vector().front().String() = "core:air";
	EXPECT_FALSE(v2(rules));
	rules = v2Rules();
	rules["factions"]["core:castle"]["preferredA"].String() = "core:air";
	EXPECT_FALSE(v2(rules));
}

TEST(NewHorizonsMagicV2SchemaTest, CurrentSettingsWrapperAcceptsBothSavedRulesVersions)
{
	const auto v1Settings = settingsWithCoreScopedMagicRoot(v1Rules());
	EXPECT_EQ(v1Settings["magic"]["newHorizons"].getModScope(), "core");
	EXPECT_EQ(v1Settings["magic"]["newHorizons"]["rulesetVersion"].getModScope(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
	EXPECT_TRUE(named(v1Settings, "vcmi:gameSettings"));
	const auto v2Settings = settingsWithCoreScopedMagicRoot(v2Rules());
	EXPECT_TRUE(named(v2Settings, "vcmi:gameSettings"));
	const auto v3Settings = settingsWithCoreScopedMagicRoot(v3Rules());
	EXPECT_TRUE(named(v3Settings, "vcmi:gameSettings"));
}

TEST(NewHorizonsMagicV2SchemaTest, AuthoredMapMagicOverrideAcceptsV3Snapshot)
{
	EXPECT_TRUE(named(v2Rules(), "vcmi:newHorizonsMapMagicOverride"));
	EXPECT_TRUE(named(v3Rules(), "vcmi:newHorizonsMapMagicOverride"));
}
