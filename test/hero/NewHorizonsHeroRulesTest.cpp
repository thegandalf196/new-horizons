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
#include "../../lib/GameConstants.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/modding/CModHandler.h"

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
	const std::map<std::string, std::pair<std::array<int, 4>, std::array<int, 4>>> expectedProfiles = {
		{"core:knight", {{15, 20, 5, 10}, {3, 4, 1, 2}}},
		{"core:cleric", {{5, 10, 15, 20}, {1, 2, 3, 4}}},
		{"core:ranger", {{15, 15, 10, 10}, {3, 3, 2, 2}}},
		{"core:druid", {{5, 5, 15, 25}, {1, 1, 3, 5}}},
		{"core:alchemist", {{15, 10, 10, 15}, {3, 2, 2, 3}}},
		{"core:wizard", {{5, 5, 20, 20}, {1, 1, 4, 4}}},
		{"core:demoniac", {{25, 10, 10, 5}, {5, 2, 2, 1}}},
		{"core:heretic", {{10, 5, 20, 15}, {2, 1, 4, 3}}},
		{"core:deathknight", {{20, 10, 15, 5}, {4, 2, 3, 1}}},
		{"core:necromancer", {{5, 10, 20, 15}, {1, 2, 4, 3}}},
		{"core:overlord", {{20, 15, 10, 5}, {4, 3, 2, 1}}},
		{"core:warlock", {{10, 5, 25, 10}, {2, 1, 5, 2}}},
		{"core:barbarian", {{25, 15, 5, 5}, {5, 3, 1, 1}}},
		{"core:battlemage", {{20, 5, 15, 10}, {4, 1, 3, 2}}},
		{"core:beastmaster", {{15, 25, 5, 5}, {3, 5, 1, 1}}},
		{"core:witch", {{5, 10, 10, 25}, {1, 2, 2, 5}}},
		{"core:planeswalker", {{15, 10, 15, 10}, {3, 2, 3, 2}}},
		{"core:elementalist", {{5, 5, 25, 15}, {1, 1, 5, 3}}}
	};
	EXPECT_EQ(rules["classProfiles"].Struct().size(), expectedProfiles.size());
	EXPECT_EQ(rules["powerDivisor"].Integer(), 10);
	EXPECT_EQ(rules["maxPrimary"].Integer(), 10000);
	for(const auto & [key, expected] : expectedProfiles)
	{
		SCOPED_TRACE(key);
		const auto profile = parsePrimaryProfile(rules["classProfiles"][key]);
		EXPECT_EQ(profile.starting, expected.first);
		EXPECT_EQ(profile.growth, expected.second);
	}
	ASSERT_EQ(rules["skillOfferWeights"].Struct().size(), expectedProfiles.size());
	for(const auto & [key, weights] : rules["skillOfferWeights"].Struct())
	{
		SCOPED_TRACE(key);
		EXPECT_EQ(weights.Struct().size(), HERO_SKILL_OFFER_COUNT);
	}
	const auto knight = parsePrimaryProfile(rules["classProfiles"]["core:knight"]);
	EXPECT_EQ(knight.baseAtLevel(20), (std::array<int64_t, 4>{72, 96, 24, 48}));
	const auto resolved = resolveHeroRules(rules, HeroClassID(HeroClassID::decode("core:knight")));
	ASSERT_NO_THROW(validateResolvedHeroRules(resolved));
	ASSERT_TRUE(resolved["skillOfferWeights"].isStruct());
	EXPECT_EQ(resolved["skillOfferWeights"].Struct().size(), HERO_SKILL_OFFER_COUNT);
	EXPECT_TRUE(skillGrowthChances(resolved, [](SecondarySkill) { return 0; }).empty());
	// The canonical data keeps extraGrowth empty.  The accessor remains for
	// loading old snapshots but never exposes skill-based primary rolls.
	EXPECT_TRUE(skillGrowthChances(resolved, [](SecondarySkill) { return 3; }).empty());
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
