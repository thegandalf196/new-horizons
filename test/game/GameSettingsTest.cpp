/*
 * GameSettingsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsMasteryRules.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
JsonNode baseSettings()
{
	JsonNode result(JsonPath::builtin("config/newHorizonsCombat"));
	result["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	return result;
}

JsonNode optionalCaptureSettings()
{
	auto result = baseSettings();
	result["heroes"]["newHorizonsCapabilities"] = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	result["heroes"]["newHorizonsMasteries"] = JsonNode(JsonPath::builtin("config/newHorizonsMasteries"));
	result["creatures"]["newHorizonsCategories"] = JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories"));
	return result;
}

void expectLegacyCaptureConsumers(const GameSettings & settings, JsonNode::JsonType expectedType)
{
	const auto heroClass = HeroClassID(HeroClassID::decode("core:knight"));
	const auto & growth = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	EXPECT_EQ(growth.getType(), expectedType);
	EXPECT_TRUE(newHorizonsHeroes::resolveHeroRules(growth, heroClass).isNull());
	const auto & capabilities = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES);
	EXPECT_EQ(capabilities.getType(), expectedType);
	EXPECT_TRUE(newHorizonsHeroes::resolveCapabilityRules(capabilities, heroClass).isNull());
	const auto & masteries = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES);
	EXPECT_EQ(masteries.getType(), expectedType);
	EXPECT_FALSE(newHorizonsHeroes::masteryOptions(masteries, SecondarySkill::ARTILLERY));
	const auto categories = newHorizonsCreatures::captureCreatureCategoryRules(
		settings.getValue(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES));
	EXPECT_EQ(categories.getRules().getType(), expectedType);
	EXPECT_FALSE(categories.lookup("core:pikeman"));
}

JsonNode originalModeSettings()
{
	JsonNode result(JsonPath::builtin("config/newHorizonsCombat"));
	result["combat"].Struct().erase("heroCommands");
	return result;
}

JsonNode targetedCommandRules()
{
	const JsonNode config(JsonPath::builtin("config/newHorizonsCombatV2"));
	return config["combat"]["heroCommands"];
}
}

TEST(GameSettings, VersionedHeroCommandOverrideReplacesCanonicalRulesAndRoundTrips)
{
	const auto base = baseSettings();
	const auto canonicalRules = base["combat"]["heroCommands"];
	const auto authoredRules = targetedCommandRules();
	ASSERT_EQ(canonicalRules["rulesetVersion"].Integer(), heroCommands::CURRENT_RULESET_VERSION);
	ASSERT_EQ(authoredRules["rulesetVersion"].Integer(), 2);

	GameSettings source;
	source.loadBase(base);
	ASSERT_EQ(source.getValue(EGameSettings::COMBAT_HERO_COMMANDS), canonicalRules);
	ASSERT_NO_THROW(heroCommands::validateRules(source.getValue(EGameSettings::COMBAT_HERO_COMMANDS)));

	source.addOverride(EGameSettings::COMBAT_HERO_COMMANDS, authoredRules);
	EXPECT_EQ(source.getValue(EGameSettings::COMBAT_HERO_COMMANDS), authoredRules);
	EXPECT_NO_THROW(heroCommands::validateRules(source.getValue(EGameSettings::COMBAT_HERO_COMMANDS)));
	EXPECT_EQ(base["combat"]["heroCommands"], canonicalRules);

	GameSettings canonical;
	canonical.loadBase(base);
	EXPECT_EQ(canonical.getValue(EGameSettings::COMBAT_HERO_COMMANDS), canonicalRules);

	CMemorySerializer memory;
	memory.oser & source;
	GameSettings restored;
	restored.loadBase(base);
	memory.iser & restored;
	EXPECT_EQ(restored.getValue(EGameSettings::COMBAT_HERO_COMMANDS), authoredRules);
	EXPECT_NO_THROW(heroCommands::validateRules(restored.getValue(EGameSettings::COMBAT_HERO_COMMANDS)));
}

TEST(GameSettings, HeroCommandOverridePreservesAbsentAndExplicitNullAcrossRoundTrip)
{
	const auto base = baseSettings();
	const auto canonicalRules = base["combat"]["heroCommands"];

	GameSettings absent;
	absent.loadBase(base);
	CMemorySerializer absentMemory;
	absentMemory.oser & absent;
	GameSettings absentRestored;
	absentRestored.loadBase(base);
	absentMemory.iser & absentRestored;
	EXPECT_EQ(absentRestored.getValue(EGameSettings::COMBAT_HERO_COMMANDS), canonicalRules);

	const auto originalBase = originalModeSettings();
	GameSettings originalMode;
	originalMode.loadBase(originalBase);
	EXPECT_TRUE(originalMode.getValue(EGameSettings::COMBAT_HERO_COMMANDS).isNull());
	CMemorySerializer originalModeMemory;
	originalModeMemory.oser & originalMode;
	GameSettings originalModeRestored;
	originalModeRestored.loadBase(originalBase);
	originalModeMemory.iser & originalModeRestored;
	EXPECT_TRUE(originalModeRestored.getValue(EGameSettings::COMBAT_HERO_COMMANDS).isNull());

	JsonNode explicitNullOverride;
	explicitNullOverride["combat"]["heroCommands"] = JsonNode();
	ASSERT_TRUE(explicitNullOverride["combat"].Struct().contains("heroCommands"));

	GameSettings explicitNull;
	explicitNull.loadBase(base);
	explicitNull.loadOverrides(explicitNullOverride);
	ASSERT_TRUE(explicitNull.getValue(EGameSettings::COMBAT_HERO_COMMANDS).isNull());
	CMemorySerializer nullMemory;
	nullMemory.oser & explicitNull;
	GameSettings nullRestored;
	nullRestored.loadBase(base);
	nullMemory.iser & nullRestored;
	EXPECT_TRUE(nullRestored.getValue(EGameSettings::COMBAT_HERO_COMMANDS).isNull());
}

TEST(GameSettings, NonVersionedSettingsOverridesStillDeepMerge)
{
	const auto base = baseSettings();
	const auto originalHeroRules = base["heroes"]["newHorizons"];
	GameSettings settings;
	settings.loadBase(base);

	JsonNode patch;
	patch["maxPrimary"].Integer() = 9000;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, patch);

	const auto & effective = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	EXPECT_EQ(effective["maxPrimary"].Integer(), 9000);
	EXPECT_EQ(effective["classProfiles"], originalHeroRules["classProfiles"]);
}

TEST(GameSettings, AbsentOptionalCapturesReachLegacyConsumers)
{
	auto base = baseSettings();
	base["heroes"].Struct().erase("newHorizons");
	GameSettings settings;
	settings.loadBase(base);
	expectLegacyCaptureConsumers(settings, JsonNode::JsonType::DATA_NULL);
}

TEST(GameSettings, WholeNullOverridesDisableLoadedOptionalCaptures)
{
	const auto base = optionalCaptureSettings();
	GameSettings settings;
	settings.loadBase(base);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS), base["heroes"]["newHorizons"]);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES), base["heroes"]["newHorizonsCapabilities"]);
	EXPECT_EQ(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES), base["heroes"]["newHorizonsMasteries"]);
	EXPECT_EQ(settings.getValue(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES), base["creatures"]["newHorizonsCategories"]);
	for(const auto option : {EGameSettings::HEROES_NEW_HORIZONS,
		EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, EGameSettings::HEROES_NEW_HORIZONS_MASTERIES,
		EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES})
		settings.addOverride(option, JsonNode());
	expectLegacyCaptureConsumers(settings, JsonNode::JsonType::DATA_NULL);
	EXPECT_EQ(base, optionalCaptureSettings());
}

TEST(GameSettings, ExplicitEmptyCapturesKeepObjectIdentityAndLegacyConsumers)
{
	GameSettings settings;
	settings.loadBase(optionalCaptureSettings());
	JsonNode empty;
	empty.Struct();
	empty.setOverrideFlag(true);
	for(const auto option : {EGameSettings::HEROES_NEW_HORIZONS,
		EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, EGameSettings::HEROES_NEW_HORIZONS_MASTERIES,
		EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES})
		settings.addOverride(option, empty);
	expectLegacyCaptureConsumers(settings, JsonNode::JsonType::DATA_STRUCT);
}
