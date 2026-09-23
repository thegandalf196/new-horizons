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
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
JsonNode baseSettings()
{
	JsonNode result(JsonPath::builtin("config/newHorizonsCombat"));
	result["heroes"]["newHorizons"] = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	return result;
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
	ASSERT_EQ(canonicalRules["rulesetVersion"].Integer(), 3);
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
