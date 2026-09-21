/*
 * NewHorizonsCapabilityRulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/json/JsonUtils.h"

#include <limits>
#include <stdexcept>

namespace
{
JsonNode resolvedCapabilities()
{
	JsonNode result;
	result["schemaVersion"].Integer() = 1;
	result["rulesetVersion"].Integer() = 1;
	result["profile"]["base"].Integer() = 2000;
	result["profile"]["perLevel"].Integer() = 200;
	for(int value : {0, 10, 20, 30})
		result["leadership"]["skillBonusPercent"].Vector().emplace_back(value);
	result["leadership"]["minimumMovementPercent"].Integer() = 50;
	for(int value : {1, 2, 3, 4})
		result["siege"]["ballistaDamageMultiplier"].Vector().emplace_back(value);
	return result;
}
}

TEST(NewHorizonsCapabilityRules, ActualCanonicalDataHasFullDeclaredMightAndMagicProfiles)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsCapabilities"));
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsCapabilities", "canonical capability data"));
	ASSERT_NO_THROW(newHorizonsHeroes::validateCapabilityRules(rules, true));
	ASSERT_EQ(rules["classProfiles"].Struct().size(), 18u);
	struct ExpectedProfile
	{
		const char * name;
		int base;
		int perLevel;
	};
	const std::array<ExpectedProfile, 18> profiles{{
		{"barbarian", 1100, 60}, {"beastmaster", 1100, 60},
		{"knight", 1025, 55}, {"demoniac", 1025, 55}, {"overlord", 1025, 55},
		{"ranger", 950, 50}, {"deathknight", 950, 50},
		{"alchemist", 875, 45}, {"battlemage", 875, 45}, {"planeswalker", 875, 45},
		{"cleric", 725, 35}, {"heretic", 725, 35}, {"necromancer", 725, 35},
		{"warlock", 725, 35}, {"witch", 725, 35},
		{"druid", 650, 30}, {"wizard", 650, 30}, {"elementalist", 650, 30}}};
	for(const auto & expected : profiles)
	{
		SCOPED_TRACE(expected.name);
		const auto saved = newHorizonsHeroes::resolveCapabilityRules(rules,
			HeroClassID(HeroClassID::decode(std::string("core:") + expected.name)));
		EXPECT_EQ(saved["profile"]["base"].Integer(), expected.base);
		EXPECT_EQ(saved["profile"]["perLevel"].Integer(), expected.perLevel);
		EXPECT_EQ(newHorizonsHeroes::capabilityLeadershipRating(saved, 4),
			expected.base + 3 * expected.perLevel);
	}
	const auto knight = newHorizonsHeroes::resolveCapabilityRules(rules,
		HeroClassID(HeroClassID::decode("core:knight")));
	EXPECT_EQ(newHorizonsHeroes::capabilityLeadershipRating(knight, 2), 1080);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsHeroes::capabilityBallistaMultiplier(knight, rank), rank + 1);
}

TEST(NewHorizonsCapabilityRules, CanonicalLeadershipUsesIndependentPerSlotLimits)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsCapabilities"));
	const auto knight = newHorizonsHeroes::resolveCapabilityRules(rules,
		HeroClassID(HeroClassID::decode("core:knight")));
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID halberdier(CreatureID::decode("core:halberdier"));
	EXPECT_EQ(newHorizonsHeroes::capabilityCreatureLeadershipRequirement(knight, pikeman), 60);
	EXPECT_EQ(newHorizonsHeroes::capabilityCreatureLeadershipRequirement(knight, halberdier), 70);

	const auto pikemanSlot = newHorizonsHeroes::capabilityLeadershipSlot(knight, 1, pikeman);
	const auto halberdierSlot = newHorizonsHeroes::capabilityLeadershipSlot(knight, 1, halberdier);
	ASSERT_TRUE(pikemanSlot);
	ASSERT_TRUE(halberdierSlot);
	EXPECT_EQ(pikemanSlot->leadership, 1025);
	EXPECT_EQ(pikemanSlot->maximum, 17);
	EXPECT_TRUE(pikemanSlot->accepts(17));
	EXPECT_FALSE(pikemanSlot->accepts(18));
	EXPECT_EQ(halberdierSlot->maximum, 14);
	// A second slot is evaluated from the full hero rating again; there is no shared budget.
	EXPECT_EQ(newHorizonsHeroes::capabilityLeadershipSlot(knight, 1, pikeman)->maximum, 17);
}

TEST(NewHorizonsCapabilityRules, CanonicalSiegeOutputsUseExactRatingFormulas)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsCapabilities"));
	const auto resolved = newHorizonsHeroes::resolveCapabilityRules(rules,
		HeroClassID(HeroClassID::decode("core:knight")));

	for(const auto & [siege, ballista, firstAid] : {
		std::tuple{0, 50, 75}, std::tuple{20, 90, 135},
		std::tuple{40, 130, 195}, std::tuple{60, 170, 255}})
	{
		SCOPED_TRACE(siege);
		EXPECT_EQ(newHorizonsHeroes::capabilitySiegeOutput(resolved, siege, "ballistaDamage"), ballista);
		EXPECT_EQ(newHorizonsHeroes::capabilitySiegeOutput(resolved, siege, "firstAidHealing"), firstAid);
	}
}

TEST(NewHorizonsCapabilityRules, LegacySnapshotsCannotAcquireCanonicalSiegeOutputs)
{
	const auto legacy = resolvedCapabilities();
	EXPECT_THROW(newHorizonsHeroes::capabilitySiegeRating(legacy, 0), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::capabilitySiegeOutput(legacy, 0, "ballistaDamage"), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::capabilitySiegeOutput(legacy, 20, "firstAidHealing"), std::runtime_error);
}

TEST(NewHorizonsCapabilityRules, NamedSchemaAndRealSettingsWrapperAcceptFullAndEmptyRejectMalformed)
{
	const JsonNode original(JsonPath::builtin("config/newHorizonsCapabilities"));
	const auto valid = [](const JsonNode & node)
	{
		return JsonUtils::validate(node, "vcmi:newHorizonsCapabilities", "native capability schema");
	};
	const auto wrapped = [](const JsonNode & node)
	{
		JsonNode settings;
		settings["heroes"]["newHorizonsCapabilities"] = node;
		return JsonUtils::validate(settings, "vcmi:gameSettings", "actual capability settings wrapper");
	};
	EXPECT_TRUE(valid(original));
	EXPECT_TRUE(wrapped(original));
	EXPECT_TRUE(valid(JsonNode(JsonMap{})));
	EXPECT_TRUE(wrapped(JsonNode(JsonMap{})));
	std::vector<JsonNode> invalid(7, original);
	invalid[0]["schemaVersion"].Integer() = 2;
	invalid[1]["classProfiles"].Struct().begin()->second["base"].Integer() = 0;
	invalid[2]["classProfiles"].Struct().begin()->second["perLevel"].Float() = 2.5;
	invalid[3]["leadership"]["globalScalePercent"].Integer() = 0;
	invalid[4]["leadership"]["upgradeMultiplierPercent"].Integer() = 0;
	invalid[5]["leadership"]["creatureRequirements"].Struct().begin()->second.Integer() = 0;
	invalid[6]["siege"]["ballistaDamageMultiplier"].Vector()[3].Integer() = 101;
	for(const auto & bad : invalid)
	{
		EXPECT_FALSE(valid(bad));
		EXPECT_FALSE(wrapped(bad));
		EXPECT_THROW(newHorizonsHeroes::validateCapabilityRules(bad, true), std::runtime_error);
	}
}

TEST(NewHorizonsCapabilityRules, WorldCoverageAndResolvedSnapshotsDoNotFollowNewDefaults)
{
	const JsonNode original(JsonPath::builtin("config/newHorizonsCapabilities"));
	auto missing = original;
	missing["classProfiles"].Struct().erase(missing["classProfiles"].Struct().begin());
	EXPECT_THROW(newHorizonsHeroes::validateCapabilityRules(missing, true), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsHeroes::validateCapabilityRules(missing, false));
	auto unknown = original;
	unknown["classProfiles"]["missing-mod:unknownClass"] = original["classProfiles"].Struct().begin()->second;
	EXPECT_THROW(newHorizonsHeroes::validateCapabilityRules(unknown, false), std::runtime_error);
	auto mutableDefaults = original;
	const auto saved = newHorizonsHeroes::resolveCapabilityRules(mutableDefaults,
		HeroClassID(HeroClassID::decode("core:knight")));
	mutableDefaults["classProfiles"].Struct().clear();
	mutableDefaults["siege"]["ballistaDamageMultiplier"].Vector()[3].Integer() = 99;
	EXPECT_EQ(newHorizonsHeroes::capabilityLeadershipRating(saved, 1), 1025);
	EXPECT_EQ(newHorizonsHeroes::capabilityBallistaMultiplier(saved, 3), 4);
	EXPECT_NO_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(saved));
}

TEST(NewHorizonsCapabilityRules, ExplicitSnapshotSuppliesClassGrowthAndSkillOnlySiege)
{
	const auto rules = resolvedCapabilities();
	EXPECT_NO_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(rules));
	EXPECT_EQ(newHorizonsHeroes::capabilityLeadership(rules, 4, 3, 0).capacity, 3380);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsHeroes::capabilityBallistaMultiplier(rules, rank), rank + 1);
	// Siege accepts no hero level, Attack, Defense or creature tier input.
	auto changed = rules;
	changed["profile"]["base"].Integer() = 1000000;
	EXPECT_EQ(newHorizonsHeroes::capabilityBallistaMultiplier(changed, 3), 4);
	EXPECT_EQ(newHorizonsHeroes::capabilityLeadership(rules, 4, 3, 0).capacity, 3380);
}

TEST(NewHorizonsCapabilityRules, AbsentIdentityIsLegacyNotPermissionToUseDefaults)
{
	const JsonNode legacy;
	EXPECT_NO_THROW(newHorizonsHeroes::validateCapabilityRules(legacy, true));
	EXPECT_NO_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(legacy));
	EXPECT_TRUE(newHorizonsHeroes::resolveCapabilityRules(legacy, HeroClassID(0)).isNull());
	EXPECT_THROW(newHorizonsHeroes::capabilityLeadership(legacy, 1, 0, 0), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::capabilityBallistaMultiplier(legacy, 0), std::runtime_error);
}

TEST(NewHorizonsCapabilityRules, RejectsUnsupportedIdentityAndMalformedOrUnboundedFields)
{
	const auto original = resolvedCapabilities();
	for(const auto * key : {"schemaVersion", "rulesetVersion"})
	{
		auto rules = original;
		rules[key].Integer() = 2;
		EXPECT_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(rules), std::runtime_error);
	}
	for(double base : {-1.0, 0.0, 2.5, 1000001.0, std::numeric_limits<double>::infinity()})
	{
		auto rules = original;
		rules["profile"]["base"].Float() = base;
		EXPECT_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(rules), std::runtime_error);
	}
	auto unknown = original;
	unknown["siege"]["heroAttackCoefficient"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(unknown), std::runtime_error);
	auto wrongShape = original;
	wrongShape["leadership"]["skillBonusPercent"].Vector().pop_back();
	EXPECT_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(wrongShape), std::runtime_error);
}

TEST(NewHorizonsCapabilityRules, RejectsOutOfRangeRanksAndUntrainedCapabilityBonuses)
{
	const auto rules = resolvedCapabilities();
	for(int rank : {-1, 4})
	{
		EXPECT_THROW(newHorizonsHeroes::capabilityLeadership(rules, 1, rank, 0), std::runtime_error);
		EXPECT_THROW(newHorizonsHeroes::capabilityBallistaMultiplier(rules, rank), std::runtime_error);
	}
	auto untrainedLeadership = rules;
	untrainedLeadership["leadership"]["skillBonusPercent"].Vector()[0].Integer() = 10;
	EXPECT_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(untrainedLeadership), std::runtime_error);
	auto untrainedSiege = rules;
	untrainedSiege["siege"]["ballistaDamageMultiplier"].Vector()[0].Integer() = 2;
	EXPECT_THROW(newHorizonsHeroes::validateResolvedCapabilityRules(untrainedSiege), std::runtime_error);
}
