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
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/GameConstants.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/modding/CModHandler.h"
#include <limits>

using namespace newHorizonsHeroes;

namespace
{
bool newHorizonsModuleActive()
{
	return vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
}
}

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

TEST(NewHorizonsHeroRulesTest, ArcanePreparationPolicyCapturesCoefficientsAndKeepsOlderAbsence)
{
	const JsonNode configured(JsonPath::builtin("config/newHorizonsHeroes"));
	auto rules = testHeroRules();
	rules["arcanePreparationSpecialty"] = configured["arcanePreparationSpecialty"];
	EXPECT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "Arcane Preparation policy"));
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	const auto captured = resolveHeroRules(rules, HeroClassID(0));
	const auto policy = arcanePreparationSpecialtyRules(captured);
	ASSERT_TRUE(policy);
	EXPECT_EQ(policy->initialPercent, 25);
	EXPECT_EQ(policy->percentPerLevel, 5);
	EXPECT_EQ(policy->maximumPercent, 100);
	EXPECT_NO_THROW(validateIndividualSpecialtiesSerialization(captured, true));
	EXPECT_THROW(validateIndividualSpecialtiesSerialization(captured, false), std::runtime_error);
	rules["arcanePreparationSpecialty"]["initialPercent"].Integer() = 50;
	EXPECT_EQ(arcanePreparationSpecialtyRules(captured)->initialPercent, 25);
	auto old = captured;
	old.Struct().erase("arcanePreparationSpecialty");
	const auto before = old;
	EXPECT_FALSE(arcanePreparationSpecialtyRules(old));
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_NO_THROW(validateIndividualSpecialtiesSerialization(old, false));
	EXPECT_EQ(old, before);
	std::vector<JsonNode> invalid(6, captured);
	invalid[0]["arcanePreparationSpecialty"]["version"].Integer() = 2;
	invalid[1]["arcanePreparationSpecialty"]["initialPercent"].Integer() = -1;
	invalid[2]["arcanePreparationSpecialty"]["percentPerLevel"].Float() = 1.5;
	invalid[3]["arcanePreparationSpecialty"]["maximumPercent"].Integer() = 101;
	invalid[4]["arcanePreparationSpecialty"]["unexpected"].Bool() = true;
	invalid[5]["arcanePreparationSpecialty"] = JsonNode();
	for(const auto & malformed : invalid)
		EXPECT_THROW(arcanePreparationSpecialtyRules(malformed), std::runtime_error);
}

TEST(NewHorizonsHeroRulesTest, FortuneChildPolicyCapturesCoefficientsAndKeepsOlderAbsence)
{
	const JsonNode configured(JsonPath::builtin("config/newHorizonsHeroes"));
	auto rules = testHeroRules();
	rules["fortuneChildSpecialty"] = configured["fortuneChildSpecialty"];
	EXPECT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "Fortune's Child policy"));
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	const auto captured = resolveHeroRules(rules, HeroClassID(0));
	ASSERT_TRUE(fortuneChildSpecialtyRules(captured));
	EXPECT_EQ(fortuneChildSpecialtyRules(captured)->luckPerStep, 1);
	EXPECT_EQ(fortuneChildSpecialtyRules(captured)->levelStep, 5);
	EXPECT_NO_THROW(validateIndividualSpecialtiesSerialization(captured, true));
	EXPECT_THROW(validateIndividualSpecialtiesSerialization(captured, false), std::runtime_error);
	rules["fortuneChildSpecialty"]["levelStep"].Integer() = 10;
	EXPECT_EQ(fortuneChildSpecialtyRules(captured)->levelStep, 5);
	auto old = captured;
	old.Struct().erase("fortuneChildSpecialty");
	const auto before = old;
	EXPECT_FALSE(fortuneChildSpecialtyRules(old));
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_NO_THROW(validateIndividualSpecialtiesSerialization(old, false));
	EXPECT_EQ(old, before);
	std::vector<JsonNode> invalid(6, captured);
	invalid[0]["fortuneChildSpecialty"]["version"].Integer() = 2;
	invalid[1]["fortuneChildSpecialty"]["levelStep"].Integer() = 0;
	invalid[2]["fortuneChildSpecialty"]["luckPerStep"].Float() = 1.5;
	invalid[3]["fortuneChildSpecialty"]["luckPerStep"].Integer() = -1;
	invalid[4]["fortuneChildSpecialty"]["unexpected"].Bool() = true;
	invalid[5]["fortuneChildSpecialty"] = JsonNode();
	for(const auto & malformed : invalid)
		EXPECT_THROW(fortuneChildSpecialtyRules(malformed), std::runtime_error);
}

TEST(NewHorizonsHeroRulesTest, DruidPolicyCapturesExactCoefficientsAndKeepsOlderAbsence)
{
	const JsonNode configured(JsonPath::builtin("config/newHorizonsHeroes"));
	auto rules = testHeroRules();
	rules["druidSpecialties"] = configured["druidSpecialties"];
	ASSERT_TRUE(rules["druidSpecialties"].isStruct());
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	const auto captured = resolveHeroRules(rules, HeroClassID(0));
	const auto policy = druidSpecialtyRules(captured);
	ASSERT_TRUE(policy);
	EXPECT_EQ(policy->highLore.percentPerLevel, 10);
	EXPECT_EQ(policy->highLore.maximumPercent, 100);
	EXPECT_EQ(policy->restorativeSorceress.percentPerLevel, 5);
	EXPECT_EQ(policy->naturalPhilosopher.percentPerLevel, 5);
	EXPECT_EQ(policy->knowledgeObjectExperiencePercent, 25);
	EXPECT_NO_THROW(validateDruidSpecialtySerialization(captured, true));
	EXPECT_THROW(validateDruidSpecialtySerialization(captured, false), std::runtime_error);
	rules["druidSpecialties"]["highLore"]["percentPerLevel"].Integer() = 20;
	EXPECT_EQ(druidSpecialtyRules(captured)->highLore.percentPerLevel, 10);
	auto old = captured;
	old.Struct().erase("druidSpecialties");
	const auto before = old;
	EXPECT_FALSE(druidSpecialtyRules(old));
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_NO_THROW(validateDruidSpecialtySerialization(old, false));
	EXPECT_EQ(old, before);

	std::vector<JsonNode> invalid(8, captured);
	invalid[0]["druidSpecialties"]["version"].Integer() = 2;
	invalid[1]["druidSpecialties"]["highLore"]["percentPerLevel"].Integer() = -1;
	invalid[2]["druidSpecialties"]["highLore"]["percentPerLevel"].Float() = 1.5;
	invalid[3]["druidSpecialties"]["restorativeSorceress"]["maximumPercent"].Integer() = 1001;
	invalid[4]["druidSpecialties"]["naturalPhilosopher"]["knowledgeObjectExperiencePercent"].Integer() = -1;
	invalid[5]["druidSpecialties"].Struct().erase("highLore");
	invalid[6]["druidSpecialties"]["unexpected"].Bool() = true;
	invalid[7]["druidSpecialties"]["highLore"] = JsonNode(false);
	for(const auto & malformed : invalid)
	{
		EXPECT_THROW(validateResolvedHeroRules(malformed), std::runtime_error);
		EXPECT_THROW(druidSpecialtyRules(malformed), std::runtime_error);
		EXPECT_THROW(validateDruidSpecialtySerialization(malformed, true), std::runtime_error);
	}
}

TEST(NewHorizonsHeroRulesTest, DruidContributionUsesIndependentFloorLevelCapAndSafeSaturation)
{
	const LevelScaledSpecialty highLore{10, 100};
	EXPECT_EQ(druidSpecialtyPercent(0, highLore), 0);
	EXPECT_EQ(druidSpecialtyPercent(1, highLore), 10);
	EXPECT_EQ(druidSpecialtyPercent(10, highLore), 100);
	EXPECT_EQ(druidSpecialtyPercent(20, highLore), 100);
	EXPECT_EQ(druidSpecialtyPercent(std::numeric_limits<int>::max(), highLore), 100);
	EXPECT_EQ(druidSpecialtyExtra(30, 10), 3);
	EXPECT_EQ(druidSpecialtyExtra(29, 10), 2);
	EXPECT_EQ(druidSpecialtyExtra(0, 100), 0);
	EXPECT_EQ(druidSpecialtyExtra(30, 0), 0);
	EXPECT_EQ(druidSpecialtyExtra(std::numeric_limits<int64_t>::max(), 100), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(druidSpecialtyExtra(std::numeric_limits<int64_t>::max(), 1000), std::numeric_limits<int64_t>::max());
	EXPECT_THROW(druidSpecialtyPercent(-1, highLore), std::invalid_argument);
	EXPECT_THROW(druidSpecialtyPercent(1, (LevelScaledSpecialty{1001, 100})), std::invalid_argument);
	EXPECT_THROW(druidSpecialtyExtra(-1, 10), std::invalid_argument);
	EXPECT_THROW(druidSpecialtyExtra(30, 1001), std::invalid_argument);
}

TEST(NewHorizonsHeroRulesTest, FreshWeightedPerkCaptureRequiresWholeClassTableButLegacyValidationDoesNot)
{
	if(!newHorizonsModuleActive())
		GTEST_SKIP() << "Requires scoped canonical New Horizons skill identities";
	const JsonNode complete(JsonPath::builtin("config/newHorizonsHeroes"));
	const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(usesWeightedPerkOffers(perks));
	ASSERT_NO_THROW(validateWeightedPerkCapture(complete, perks));
	auto missing = complete;
	missing.Struct().erase("skillOfferWeights");
	EXPECT_THROW(validateWeightedPerkCapture(missing, perks), std::runtime_error);
	missing["skillOfferWeights"] = JsonNode();
	EXPECT_THROW(validateWeightedPerkCapture(missing, perks), std::runtime_error);
	EXPECT_THROW(validateWeightedPerkCapture(JsonNode(), perks), std::runtime_error);
	EXPECT_THROW(validateWeightedPerkCapture(JsonNode(JsonMap{}), perks), std::runtime_error);
	auto partial = complete;
	ASSERT_GT(partial["skillOfferWeights"].Struct().size(), 1);
	partial["skillOfferWeights"].Struct().erase(partial["skillOfferWeights"].Struct().begin());
	EXPECT_THROW(validateWeightedPerkCapture(partial, perks), std::runtime_error);
	partial = complete;
	auto & row = partial["skillOfferWeights"].Struct().begin()->second.Struct();
	row.erase(row.begin());
	EXPECT_THROW(validateWeightedPerkCapture(partial, perks), std::runtime_error);
	auto legacyPerks = perks;
	legacyPerks.Struct().erase("offerWeightPolicyVersion");
	const auto before = missing;
	EXPECT_NO_THROW(validateWeightedPerkCapture(missing, legacyPerks));
	EXPECT_NO_THROW(validateHeroRules(missing, true));
	EXPECT_NO_THROW(validateWeightedPerkCapture(testHeroRules(), legacyPerks));
	EXPECT_EQ(missing, before);
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

TEST(NewHorizonsHeroRulesTest, ExplicitProgressionVersionKeepsOldAndNewSnapshotsIndependent)
{
	auto rules = testHeroRules();
	const auto oldSnapshot = resolveHeroRules(rules, HeroClassID(0));
	auto & profile = rules["classProfiles"][HeroClassID::encode(0)];
	profile["progressionVersion"].Integer() = 2;
	profile["starting"].Vector().clear();
	profile["growth"].Vector().clear();
	for(int value : {30, 45, 10, 15})
		profile["starting"].Vector().push_back(JsonNode(value));
	for(int value : {6, 7, 2, 3})
		profile["growth"].Vector().push_back(JsonNode(value));
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	EXPECT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "versioned primary profiles"));
	const auto newSnapshot = resolveHeroRules(rules, HeroClassID(0));
	EXPECT_NO_THROW(validateResolvedHeroRules(oldSnapshot));
	EXPECT_NO_THROW(validateResolvedHeroRules(newSnapshot));
	EXPECT_EQ(parsePrimaryProfile(oldSnapshot["profile"]).baseAtLevel(1), (std::array<int64_t, 4>{20, 20, 5, 5}));
	EXPECT_EQ(parsePrimaryProfile(newSnapshot["profile"]).baseAtLevel(1), (std::array<int64_t, 4>{30, 45, 10, 15}));
	EXPECT_EQ(parsePrimaryProfile(newSnapshot["profile"]).baseAtLevel(2), (std::array<int64_t, 4>{36, 52, 12, 18}));
	profile["progressionVersion"].Integer() = 3;
	profile["growth"].Vector()[1].Integer() = 9;
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	EXPECT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "twenty-point primary profiles"));
	const auto canonicalSnapshot = resolveHeroRules(rules, HeroClassID(0));
	EXPECT_NO_THROW(validateResolvedHeroRules(canonicalSnapshot));
	EXPECT_EQ(parsePrimaryProfile(canonicalSnapshot["profile"]).baseAtLevel(2), (std::array<int64_t, 4>{36, 54, 12, 18}));
	profile["progressionVersion"].Integer() = 4;
	EXPECT_FALSE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "unsupported primary progression"));
	EXPECT_THROW(validateHeroRules(rules, true), std::runtime_error);
	EXPECT_EQ(newSnapshot["profile"]["progressionVersion"].Integer(), 2);
	EXPECT_TRUE(oldSnapshot["profile"]["progressionVersion"].isNull());
}

TEST(NewHorizonsHeroRulesTest, VersionThreeUsesOnlyOwnedConfiguredSkillGrowthChances)
{
	auto rules = testHeroRules();
	auto & profile = rules["classProfiles"][HeroClassID::encode(0)];
	profile["progressionVersion"].Integer() = 3;
	profile["starting"].Vector().clear();
	profile["growth"].Vector().clear();
	for(int value : {30, 45, 10, 15})
		profile["starting"].Vector().push_back(JsonNode(value));
	for(int value : {6, 9, 2, 3})
		profile["growth"].Vector().push_back(JsonNode(value));
	for(auto & extra : rules["extraGrowth"].Vector())
		for(int rank = 0; rank <= 3; ++rank)
			extra["chances"].Vector()[rank].Integer() = rank * 10;
	const auto resolved = resolveHeroRules(rules, HeroClassID(0));
	EXPECT_TRUE(skillGrowthChances(resolved, [](SecondarySkill) { return 0; }).empty());
	const auto chances = skillGrowthChances(resolved, [](SecondarySkill skill)
	{
		return skill == SecondarySkill::OFFENCE ? 2 : 0;
	});
	ASSERT_EQ(chances.size(), 1);
	EXPECT_EQ(chances.front().skill, SecondarySkill::OFFENCE);
	EXPECT_EQ(chances.front().attribute, PrimarySkill::ATTACK);
	EXPECT_EQ(chances.front().chancePercent, 20);
	EXPECT_THROW(skillGrowthChances(resolved, [](SecondarySkill) { return 4; }), std::runtime_error);
}

TEST(NewHorizonsHeroRulesTest, OldResolvedSnapshotWithoutMigrationTableRemainsLoadable)
{
	if(!newHorizonsModuleActive())
		GTEST_SKIP() << "Requires the New Horizons module for scoped canonical skills";
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_NO_THROW(validateHeroRules(rules, true));

	const auto resolved = resolveHeroRules(rules,
		HeroClassID(HeroClassID::decode("core:knight")));
	auto oldSnapshot = resolved;
	oldSnapshot["startingSkills"].Struct().erase("legacySkillMigrations");
	EXPECT_NO_THROW(validateResolvedHeroRules(oldSnapshot));

	const std::vector<std::pair<SecondarySkill, ui8>> oldSkills = {
		{SecondarySkill::ARCHERY, MasteryLevel::BASIC}};
	EXPECT_EQ(migrateStartingSkills(oldSnapshot,
		FactionID(FactionID::decode("core:castle")), oldSkills), oldSkills);

	// The installed canonical profile is still required to carry the complete
	// table; only an already-resolved saved snapshot may omit it.
	rules["startingSkills"].Struct().erase("legacySkillMigrations");
	EXPECT_THROW(validateHeroRules(rules, true), std::runtime_error);
}

TEST(NewHorizonsHeroRulesTest, ActualCanonicalDataHasExactClassProfilesAndSkillOpportunities)
{
	if(!newHorizonsModuleActive())
		GTEST_SKIP() << "Requires the New Horizons module for scoped canonical skills";
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "actual unactivated canonical hero data"));
	ASSERT_NO_THROW(validateHeroRules(rules, true));
	const std::map<std::string, std::array<int, 4>> expectedProfiles = {
		{"core:knight", {30, 45, 10, 15}},
		{"core:cleric", {10, 15, 30, 45}},
		{"core:ranger", {35, 35, 15, 15}},
		{"core:druid", {5, 10, 30, 55}},
		{"core:alchemist", {30, 20, 20, 30}},
		{"core:wizard", {5, 5, 45, 45}},
		{"core:demoniac", {55, 20, 20, 5}},
		{"core:heretic", {20, 5, 50, 25}},
		{"core:deathknight", {45, 20, 30, 5}},
		{"core:necromancer", {5, 20, 50, 25}},
		{"core:overlord", {50, 25, 20, 5}},
		{"core:warlock", {15, 5, 60, 20}},
		{"core:barbarian", {55, 35, 5, 5}},
		{"core:battlemage", {45, 5, 30, 20}},
		{"core:beastmaster", {35, 55, 5, 5}},
		{"core:witch", {5, 15, 20, 60}},
		{"core:planeswalker", {35, 20, 30, 15}},
		{"core:elementalist", {5, 5, 60, 30}}
	};
	EXPECT_EQ(rules["classProfiles"].Struct().size(), expectedProfiles.size());
	EXPECT_EQ(rules["powerDivisor"].Integer(), 10);
	EXPECT_EQ(rules["maxPrimary"].Integer(), 10000);
	for(const auto & [key, expected] : expectedProfiles)
	{
		SCOPED_TRACE(key);
		const auto profile = parsePrimaryProfile(rules["classProfiles"][key]);
		EXPECT_EQ(profile.progressionVersion, PRIMARY_PROFILE_VERSION_TWENTY_POINT);
		EXPECT_EQ(profile.starting, expected);
		for(size_t attribute = 0; attribute < profile.growth.size(); ++attribute)
			EXPECT_EQ(profile.growth[attribute], expected[attribute] / 5);
	}
	ASSERT_EQ(rules["skillOfferWeights"].Struct().size(), expectedProfiles.size());
	for(const auto & [key, weights] : rules["skillOfferWeights"].Struct())
	{
		SCOPED_TRACE(key);
		EXPECT_EQ(weights.Struct().size(), HERO_SKILL_OFFER_COUNT);
	}
	const auto knight = parsePrimaryProfile(rules["classProfiles"]["core:knight"]);
	EXPECT_EQ(knight.baseAtLevel(20), (std::array<int64_t, 4>{144, 216, 48, 72}));
	const auto resolved = resolveHeroRules(rules, HeroClassID(HeroClassID::decode("core:knight")));
	ASSERT_NO_THROW(validateResolvedHeroRules(resolved));
	ASSERT_TRUE(resolved["skillOfferWeights"].isStruct());
	EXPECT_EQ(resolved["skillOfferWeights"].Struct().size(), HERO_SKILL_OFFER_COUNT);
	EXPECT_TRUE(skillGrowthChances(resolved, [](SecondarySkill) { return 0; }).empty());
	const auto expertChances = skillGrowthChances(resolved, [](SecondarySkill) { return 3; });
	ASSERT_EQ(expertChances.size(), 5);
	for(const auto & chance : expertChances)
		EXPECT_EQ(chance.chancePercent, 30);
	const SecondarySkill offense(SecondarySkill::decode("new-horizons:offense"));
	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	EXPECT_TRUE(usesSkillOfferWeights(resolved));
	EXPECT_EQ(skillOfferWeight(resolved, offense), 4);
	EXPECT_EQ(skillOfferWeight(resolved, wisdom), 0);
	EXPECT_TRUE(isExcludedSkill(resolved, SecondarySkill::MYSTICISM));
	EXPECT_FALSE(isExcludedSkill(resolved, offense));
}

TEST(NewHorizonsHeroRulesTest, CanonicalFactionSkillMappingCoversAllFactionsAndAliases)
{
	if(!newHorizonsModuleActive())
		GTEST_SKIP() << "Requires the New Horizons module for scoped canonical skills";
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto resolved = resolveHeroRules(rules, HeroClassID(HeroClassID::decode("core:knight")));
	const std::array<std::pair<const char *, const char *>, 9> expected = {{
		{"core:castle", "new-horizons:divineMandate"},
		{"core:rampart", "new-horizons:sylvanLuck"},
		{"core:tower", "new-horizons:metamagic"},
		{"core:inferno", "new-horizons:demonicGating"},
		{"core:necropolis", "new-horizons:necromancy"},
		{"core:dungeon", "new-horizons:shroudOfMalassa"},
		{"core:stronghold", "new-horizons:bloodrage"},
		{"core:fortress", "new-horizons:bulwarkOfTheMire"},
		{"core:conflux", "new-horizons:elementalRebirth"},
	}};

	for(const auto & [factionId, skillId] : expected)
	{
		SCOPED_TRACE(factionId);
		const FactionID faction(FactionID::decode(factionId));
		const SecondarySkill skill(SecondarySkill::decode(skillId));
		const auto mapped = factionSkill(resolved, faction);
		ASSERT_TRUE(mapped.has_value());
		EXPECT_EQ(*mapped, skill);
		EXPECT_TRUE(isFactionSkill(resolved, skill));
		EXPECT_TRUE(isFactionSkillForFaction(resolved, faction, skill));
	}

	const auto necropolis = FactionID(FactionID::decode("core:necropolis"));
	EXPECT_TRUE(isFactionSkillForFaction(resolved, necropolis, SecondarySkill::NECROMANCY));
	EXPECT_FALSE(factionSkill(JsonNode(), necropolis).has_value());
	const std::vector<std::pair<SecondarySkill, ui8>> oldSaveSkills = {{SecondarySkill::NECROMANCY, MasteryLevel::BASIC}};
	EXPECT_EQ(applyStartingFactionSkill(JsonNode(), false, necropolis, oldSaveSkills), oldSaveSkills);
}

TEST(NewHorizonsHeroRulesTest, FactionStartingSkillsReplaceWisdomOrOptionalMightSkill)
{
	if(!newHorizonsModuleActive())
		GTEST_SKIP() << "Requires the New Horizons module for scoped canonical skills";
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

TEST(NewHorizonsHeroRulesTest, MightMigrationReplacesLegacyWisdomWithSpellcraft)
{
	if(!newHorizonsModuleActive())
		GTEST_SKIP() << "Requires the New Horizons module for scoped canonical skills";
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto resolved = resolveHeroRules(rules, HeroClassID(HeroClassID::decode("core:demoniac")));
	const auto inferno = FactionID(FactionID::decode("core:inferno"));
	const auto migrated = migrateStartingSkills(resolved, inferno,
		{{SecondarySkill::WISDOM, MasteryLevel::BASIC},
		 {SecondarySkill::SCHOLAR, MasteryLevel::BASIC}});
	ASSERT_EQ(migrated.size(), 1u);
	EXPECT_EQ(migrated.front().first,
		SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")));

	const auto fresh = applyStartingFactionSkill(resolved, false, inferno, migrated);
	ASSERT_EQ(fresh.size(), 2u);
	EXPECT_EQ(fresh[0].first,
		SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")));
	EXPECT_EQ(fresh[1].first,
		SecondarySkill(SecondarySkill::decode("new-horizons:demonicGating")));
	EXPECT_EQ(fresh[0].second, MasteryLevel::BASIC);
	EXPECT_EQ(fresh[1].second, MasteryLevel::BASIC);
	EXPECT_FALSE(std::any_of(fresh.begin(), fresh.end(),
		[](const auto & entry) { return entry.first == SecondarySkill::WISDOM; }));
}

TEST(NewHorizonsHeroRulesTest, NecropolisLegacySkillBecomesScopedFactionSkill)
{
	if(!newHorizonsModuleActive())
		GTEST_SKIP() << "Requires the New Horizons module for scoped canonical skills";
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
