/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HistoricalCapabilityRules.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/serializer/CMemorySerializer.h"

TEST(NewHorizonsCommandCapability, CurrentV5ResolvesAllClassesWithoutRetiredTables)
{
	using namespace newHorizonsHeroes;
	const JsonNode world(JsonPath::builtin("config/newHorizonsCapabilities"));
	ASSERT_EQ(world["rulesetVersion"].Integer(), 5);
	ASSERT_NO_THROW(validateCapabilityRules(world, true));
	EXPECT_TRUE(JsonUtils::validate(world, "vcmi:newHorizonsCapabilities", "Command V5"));
	EXPECT_TRUE(isWorldCapabilityRules(world));
	EXPECT_FALSE(world.Struct().contains("classProfiles"));
	EXPECT_FALSE(world.Struct().contains("leadership"));
	ASSERT_EQ(world["armyCommand"]["classLevelIntervals"].Struct().size(), 18u);
	for(const auto & [key, interval] : world["armyCommand"]["classLevelIntervals"].Struct())
	{
		SCOPED_TRACE(key);
		const auto saved = resolveCapabilityRules(world, HeroClassID(HeroClassID::decode(key)));
		EXPECT_FALSE(isWorldCapabilityRules(saved));
		EXPECT_FALSE(saved.Struct().contains("profile"));
		EXPECT_FALSE(saved.Struct().contains("leadership"));
		ASSERT_TRUE(capturedCommandRules(saved));
		EXPECT_EQ(capturedCommandRules(saved)->levelInterval, interval.Integer());
		EXPECT_NO_THROW(validateArmyCommandSerialization(world, true));
		EXPECT_NO_THROW(validateArmyCommandSerialization(saved, true));
		EXPECT_THROW(validateArmyCommandSerialization(saved, false), std::runtime_error);
		CMemorySerializer wire;
		wire.oser & saved;
		JsonNode restored;
		wire.iser & restored;
		EXPECT_EQ(restored, saved);
		EXPECT_NO_THROW(validateResolvedCapabilityRules(restored));
		EXPECT_EQ(capabilityLeadershipRating(restored, 100), 0);
		EXPECT_EQ(capabilityLeadershipPerLevel(restored), 0);
		EXPECT_EQ(capabilityCreatureLeadershipRequirement(restored, CreatureID(0)), 0);
		EXPECT_FALSE(capabilityLeadershipSlot(restored, 100, CreatureID(0)));
	}
}

TEST(NewHorizonsCommandCapability, CurrentPolicyCannotLeakHistoricalNumericalRatings)
{
	using namespace newHorizonsHeroes;
	const JsonNode current(JsonPath::builtin("config/newHorizonsCapabilities"));
	auto historical = historicalCapabilityRulesV4();
	const auto id = HeroClassID(HeroClassID::decode("core:knight"));
	EXPECT_EQ(capabilityLeadershipRating(resolveCapabilityRules(historical, id), 2), 1080);
	historical["armyCommand"] = current["armyCommand"];
	for(const int base : {1, 1000000})
	{
		historical["classProfiles"]["core:knight"]["base"].Integer() = base;
		historical["classProfiles"]["core:knight"]["perLevel"].Integer() = base;
		historical["leadership"]["globalScalePercent"].Integer() = 10000;
		const auto resolved = resolveCapabilityRules(historical, id);
		EXPECT_EQ(capabilityLeadershipRating(resolved, 50), 0);
		EXPECT_EQ(capabilityLeadershipPerLevel(resolved), 0);
		EXPECT_EQ(capabilityCreatureLeadershipRequirement(resolved, CreatureID(0)), 0);
	}
}

TEST(NewHorizonsCommandCapability, NativeSettingsSchemaRejectsUnknownAndInvalidClassIntervals)
{
	const JsonNode current(JsonPath::builtin("config/newHorizonsCapabilities"));
	JsonNode settings;
	settings["heroes"]["newHorizonsCapabilities"] = current;
	ASSERT_TRUE(JsonUtils::validate(settings, "vcmi:gameSettings", "Command settings wrapper"));
	for(const double interval : {0.0, 3.5, 1000001.0})
	{
		auto invalid = current;
		invalid["armyCommand"]["classLevelIntervals"]["core:knight"].Float() = interval;
		EXPECT_FALSE(JsonUtils::validate(invalid, "vcmi:newHorizonsCapabilities", "Invalid Command interval"));
	}
	auto unknown = current;
	unknown["armyCommand"]["classLevelIntervals"]["core:unknownClass"].Integer() = 3;
	EXPECT_FALSE(JsonUtils::validate(unknown, "vcmi:newHorizonsCapabilities", "Unknown Command class"));
}

TEST(NewHorizonsCommandCapability, V5RejectsRetiredColumnsAndMissingOrMalformedCommand)
{
	using namespace newHorizonsHeroes;
	const JsonNode current(JsonPath::builtin("config/newHorizonsCapabilities"));
	for(const auto * key : {"classProfiles", "leadership", "profile"})
	{
		auto invalid = current;
		invalid[key] = JsonNode(); // Presence, including null, is forbidden.
		EXPECT_THROW(validateCapabilityRules(invalid, true), std::runtime_error);
	}
	auto missing = current;
	missing.Struct().erase("armyCommand");
	EXPECT_THROW(validateCapabilityRules(missing, true), std::runtime_error);
	auto malformed = current;
	malformed["armyCommand"] = JsonNode();
	EXPECT_THROW(validateCapabilityRules(malformed, true), std::runtime_error);
	const auto id = HeroClassID(HeroClassID::decode("core:knight"));
	auto resolved = resolveCapabilityRules(current, id);
	resolved["profile"]["base"].Integer() = 1025;
	EXPECT_THROW(validateResolvedCapabilityRules(resolved), std::runtime_error);
}

TEST(NewHorizonsCommandCapability, V5PreservesAllCapturedSiegeAndShopValues)
{
	using namespace newHorizonsHeroes;
	const JsonNode current(JsonPath::builtin("config/newHorizonsCapabilities"));
	const auto historical = historicalCapabilityRulesV4();
	const auto id = HeroClassID(HeroClassID::decode("core:knight"));
	const auto now = resolveCapabilityRules(current, id);
	const auto old = resolveCapabilityRules(historical, id);
	EXPECT_EQ(now["siege"], old["siege"]);
	EXPECT_EQ(now["warMachineShop"], old["warMachineShop"]);
	for(int rank = 0; rank <= 3; ++rank)
	{
		EXPECT_EQ(capabilityBallistaMultiplier(now, rank), capabilityBallistaMultiplier(old, rank));
		EXPECT_EQ(capabilitySiegeRating(now, rank), rank * 20);
		EXPECT_EQ(capabilityDirectControlChance(now, rank), rank ? 100 : 0);
		for(const auto * output : {"ballistaDamage", "catapultStructuralDamage", "firstAidHealing", "defensiveTowerDamage"})
			EXPECT_EQ(capabilitySiegeOutput(now, rank * 20, output), capabilitySiegeOutput(old, rank * 20, output));
	}
	for(const int rank : {-1, 4})
		EXPECT_THROW(capabilitySiegeRating(now, rank), std::runtime_error);
}
