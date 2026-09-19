/*
 * NewHorizonsHeroRulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/json/JsonUtils.h"

using namespace newHorizonsHeroes;

TEST(NewHorizonsHeroRulesTest, NamedSchemaAcceptsFullAndEmptyAndRejectsMalformedFields)
{
	const auto valid = [](const JsonNode & node)
	{
		return JsonUtils::validate(node, "vcmi:newHorizonsHeroes", "native future hero schema");
	};
	EXPECT_TRUE(valid(testHeroRules()));
	EXPECT_TRUE(valid(JsonNode(JsonMap{})));
	JsonNode settings;
	settings["heroes"]["newHorizons"] = testHeroRules();
	EXPECT_TRUE(JsonUtils::validate(settings, "vcmi:gameSettings", "actual hero settings wrapper"));
	settings["heroes"]["newHorizons"] = JsonNode(JsonMap{});
	EXPECT_TRUE(JsonUtils::validate(settings, "vcmi:gameSettings", "legacy empty hero settings wrapper"));
	settings["heroes"]["newHorizons"] = testHeroRules();
	settings["heroes"]["newHorizons"]["powerDivisor"].Integer() = 0;
	EXPECT_FALSE(JsonUtils::validate(settings, "vcmi:gameSettings", "invalid nested hero divisor"));
	settings["heroes"]["newHorizons"] = testHeroRules();
	settings["heroes"]["newHorizons"]["classProfiles"].Struct().begin()->second["growth"].Vector().pop_back();
	EXPECT_FALSE(JsonUtils::validate(settings, "vcmi:gameSettings", "invalid nested hero profile"));
	auto bad = testHeroRules();
	bad["powerDivisor"].Integer() = 0;
	EXPECT_FALSE(valid(bad));
	bad = testHeroRules();
	bad["schemaVersion"].Integer() = 2;
	EXPECT_FALSE(valid(bad));
	bad = testHeroRules();
	bad["classProfiles"].Struct().begin()->second["growth"].Vector().pop_back();
	EXPECT_FALSE(valid(bad));
	bad = testHeroRules();
	bad["extraGrowth"].Vector()[0]["chances"].Vector()[1].Integer() = 101;
	EXPECT_FALSE(valid(bad));
}

TEST(NewHorizonsHeroRulesTest, ResolvedSnapshotDoesNotFollowChangedInstalledProfile)
{
	auto rules = testHeroRules();
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	const auto saved = resolveHeroRules(rules, HeroClassID(0));
	rules["powerDivisor"].Integer() = 20;
	rules["classProfiles"].Struct().clear();
	EXPECT_EQ(saved["powerDivisor"].Integer(), 10);
	EXPECT_EQ(parsePrimaryProfile(saved["profile"]).growth, (std::array<int, 4>{4, 4, 1, 1}));
	EXPECT_NO_THROW(validateResolvedHeroRules(saved));
	EXPECT_FALSE(usesRules(resolveHeroRules(JsonNode(), HeroClassID(0))));
}

TEST(NewHorizonsHeroRulesTest, ActualCanonicalDataHasCompleteProvisionalProfilesAndSkillOpportunities)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "actual unactivated canonical hero data"));
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	EXPECT_EQ(rules["classProfiles"].Struct().size(), 18u);
	EXPECT_EQ(rules["powerDivisor"].Integer(), 10);
	EXPECT_EQ(rules["maxPrimary"].Integer(), 10000);
	for(const auto & [key, data] : rules["classProfiles"].Struct())
	{
		SCOPED_TRACE(key);
		const auto profile = parsePrimaryProfile(data);
		auto start = profile.starting;
		auto growth = profile.growth;
		std::sort(start.begin(), start.end());
		std::sort(growth.begin(), growth.end());
		EXPECT_EQ(start, (std::array<int, 4>{5, 10, 15, 20}));
		EXPECT_EQ(growth, (std::array<int, 4>{1, 2, 3, 4}));
		for(int i = 0; i < 4; ++i)
			EXPECT_EQ(profile.starting[i], 5 * profile.growth[i]);
	}
	const auto knight = parsePrimaryProfile(rules["classProfiles"]["core:knight"]);
	EXPECT_EQ(knight.baseAtLevel(20), (std::array<int64_t, 4>{72, 96, 24, 48}));
	const auto resolved = resolveHeroRules(rules, HeroClassID(HeroClassID::decode("core:knight")));
	EXPECT_TRUE(skillGrowthChances(resolved, [](SecondarySkill) { return 0; }).empty());
	const auto opportunities = skillGrowthChances(resolved, [](SecondarySkill) { return 3; });
	ASSERT_EQ(opportunities.size(), 4u);
	const SecondarySkill offense(SecondarySkill::decode("new-horizons:offense"));
	EXPECT_NE(offense, SecondarySkill::OFFENCE);
	const std::array<SecondarySkill, 4> skills = {offense, SecondarySkill::ARMORER,
		SecondarySkill::SORCERY, SecondarySkill::INTELLIGENCE};
	for(int i = 0; i < 4; ++i)
	{
		EXPECT_EQ(opportunities[i].skill, skills[i]);
		EXPECT_EQ(opportunities[i].attribute, PrimarySkill(i));
		EXPECT_EQ(opportunities[i].chancePercent, 30);
	}
}

TEST(NewHorizonsHeroRulesTest, FactionStartingSkillsReplaceWisdomOrOptionalMightSkill)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto resolvedCleric = resolveHeroRules(rules, HeroClassID(HeroClassID::decode("core:cleric")));
	const auto cleric = applyStartingFactionSkill(resolvedCleric, true,
		FactionID(FactionID::decode("core:castle")),
		{{SecondarySkill::WISDOM, MasteryLevel::BASIC}, {SecondarySkill::FIRST_AID, MasteryLevel::BASIC}});
	ASSERT_EQ(cleric.size(), 2u);
	EXPECT_EQ(cleric[0].first, SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")));
	EXPECT_EQ(cleric[0].second, MasteryLevel::BASIC);
	EXPECT_EQ(cleric[1].first, SecondarySkill::FIRST_AID);

	const auto knight = applyStartingFactionSkill(resolvedCleric, false,
		FactionID(FactionID::decode("core:castle")),
		{{SecondarySkill::LEADERSHIP, MasteryLevel::BASIC}, {SecondarySkill::ARCHERY, MasteryLevel::BASIC}});
	ASSERT_EQ(knight.size(), 2u);
	EXPECT_EQ(knight[0].first, SecondarySkill::LEADERSHIP);
	EXPECT_EQ(knight[1].first, SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")));

	const auto oneSkill = applyStartingFactionSkill(resolvedCleric, false,
		FactionID(FactionID::decode("core:rampart")),
		{{SecondarySkill::ARCHERY, MasteryLevel::ADVANCED}});
	ASSERT_EQ(oneSkill.size(), 2u);
	EXPECT_EQ(oneSkill[0].first, SecondarySkill::ARCHERY);
	EXPECT_EQ(oneSkill[0].second, MasteryLevel::ADVANCED);
	EXPECT_EQ(oneSkill[1].first, SecondarySkill(SecondarySkill::decode("new-horizons:sylvanLuck")));
	EXPECT_EQ(oneSkill[1].second, MasteryLevel::BASIC);

	const auto reversedMagic = applyStartingFactionSkill(resolvedCleric, true,
		FactionID(FactionID::decode("core:castle")),
		{{SecondarySkill::FIRST_AID, MasteryLevel::BASIC}, {SecondarySkill::WISDOM, MasteryLevel::ADVANCED}});
	ASSERT_EQ(reversedMagic.size(), 2u);
	EXPECT_EQ(reversedMagic[0].first, SecondarySkill::FIRST_AID);
	EXPECT_EQ(reversedMagic[1].first, SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")));
	EXPECT_EQ(reversedMagic[1].second, MasteryLevel::ADVANCED);
}

TEST(NewHorizonsHeroRulesTest, NecropolisLegacySkillBecomesScopedFactionSkill)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto resolved = resolveHeroRules(rules, HeroClassID(HeroClassID::decode("core:deathknight")));
	const auto skills = applyStartingFactionSkill(resolved, false,
		FactionID(FactionID::decode("core:necropolis")),
		{{SecondarySkill::NECROMANCY, MasteryLevel::BASIC}, {SecondarySkill::RESISTANCE, MasteryLevel::BASIC}});
	ASSERT_EQ(skills.size(), 2u);
	EXPECT_EQ(skills[0].first, SecondarySkill(SecondarySkill::decode("new-horizons:necromancy")));
	EXPECT_EQ(skills[1].first, SecondarySkill::RESISTANCE);

	const auto aliasAndCanonical = applyStartingFactionSkill(resolved, false,
		FactionID(FactionID::decode("core:necropolis")),
		{{SecondarySkill::RESISTANCE, MasteryLevel::BASIC},
		 {SecondarySkill::NECROMANCY, MasteryLevel::BASIC},
		 {SecondarySkill(SecondarySkill::decode("new-horizons:necromancy")), MasteryLevel::EXPERT}});
	ASSERT_EQ(aliasAndCanonical.size(), 2u);
	EXPECT_EQ(aliasAndCanonical[0].first, SecondarySkill::RESISTANCE);
	EXPECT_EQ(aliasAndCanonical[1].first, SecondarySkill(SecondarySkill::decode("new-horizons:necromancy")));
	EXPECT_EQ(aliasAndCanonical[1].second, MasteryLevel::EXPERT);

	const auto magicSkills = applyStartingFactionSkill(resolved, true,
		FactionID(FactionID::decode("core:necropolis")),
		{{SecondarySkill::NECROMANCY, MasteryLevel::BASIC}, {SecondarySkill::WISDOM, MasteryLevel::BASIC}});
	ASSERT_EQ(magicSkills.size(), 1u);
	EXPECT_EQ(magicSkills[0].first, SecondarySkill(SecondarySkill::decode("new-horizons:necromancy")));
}

TEST(NewHorizonsHeroRulesTest, RuntimeChecksSemanticsBeyondSchema)
{
	auto rules = testHeroRules();
	rules["classProfiles"].Struct().erase(rules["classProfiles"].Struct().begin());
	EXPECT_THROW(validateHeroRules(rules, true), std::runtime_error);
	EXPECT_NO_THROW(validateHeroRules(rules, false));
	rules = testHeroRules();
	rules["classProfiles"].Struct().begin()->second["growth"].Vector()[0].Integer() = 5;
	EXPECT_THROW(validateHeroRules(rules, true), std::runtime_error);
	rules = testHeroRules();
	rules["extraGrowth"].Vector()[0]["chances"].Vector()[0].Integer() = 1;
	EXPECT_THROW(validateHeroRules(rules, true), std::runtime_error);
	rules = testHeroRules();
	rules["extraGrowth"].Vector()[0]["skill"].String() = "missing-mod:noSuchSkill";
	EXPECT_THROW(validateHeroRules(rules, true), std::runtime_error);
	rules = testHeroRules();
	rules["classProfiles"].Struct().begin()->second["starting"].Vector()[0].Integer() = 1000001;
	EXPECT_THROW(validateHeroRules(rules, true), std::runtime_error);
}
