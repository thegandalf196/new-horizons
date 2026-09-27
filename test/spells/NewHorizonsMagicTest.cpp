/*
 * NewHorizonsMagicTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/serializer/CMemorySerializer.h"

TEST(NewHorizonsMagicTest, LegacySchoolsCostsAndLevelsRemainOriginal)
{
	const JsonNode legacy;
	const SpellID arrow(SpellID::MAGIC_ARROW);
	const auto * definition = arrow.toSpell();
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(legacy));
	EXPECT_EQ(newHorizonsMagic::activeSchools(legacy).size(), 4u);
	const std::vector<SpellSchool> original(definition->schools.begin(), definition->schools.end());
	EXPECT_EQ(newHorizonsMagic::spellSchools(legacy, arrow), original);
	EXPECT_EQ(newHorizonsMagic::spellLevel(legacy, arrow), definition->getLevel());
	for(int mastery = 0; mastery <= 3; ++mastery)
		EXPECT_EQ(newHorizonsMagic::spellCost(legacy, arrow, mastery), definition->getCost(mastery));
}

TEST(NewHorizonsMagicTest, PhysicalReductionOptInIsValidatedAndAbsentForOlderSnapshots)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(rules), 80);
	rules.Struct().erase("physicalDamageReductionCapPercent");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(rules), -1);
	EXPECT_EQ(newHorizonsMagic::physicalDamageReductionCapPercent(JsonNode()), -1);
	for(const int invalid : {-1, 101})
	{
		rules["physicalDamageReductionCapPercent"].Integer() = invalid;
		EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	}
	rules["physicalDamageReductionCapPercent"].Float() = 80.5;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicTest, FixedSchoolMageGuildGenerationIsSavedAndValidated)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	ASSERT_TRUE(newHorizonsMagic::mageGuildGenerationActive(rules));
	const std::array<int, 5> expected{5, 4, 2, 2, 2};
	for(int level = 1; level <= 5; ++level)
		EXPECT_EQ(newHorizonsMagic::mageGuildSpellsAtLevel(rules, level), expected.at(level - 1));
	EXPECT_EQ(newHorizonsMagic::mageGuildSpellsAtLevel(rules, 0), 0);
	EXPECT_EQ(newHorizonsMagic::mageGuildSpellsAtLevel(rules, 6), 0);

	const std::array factions{
		FactionID::CASTLE, FactionID::RAMPART, FactionID::TOWER,
		FactionID::INFERNO, FactionID::NECROPOLIS, FactionID::DUNGEON,
		FactionID::STRONGHOLD, FactionID::FORTRESS, FactionID::CONFLUX};
	const std::array<std::array<const char *, 2>, 9> expectedPreferences{{
		{{"new-horizons:light", "new-horizons:sorcery"}},
		{{"new-horizons:nature", "new-horizons:light"}},
		{{"new-horizons:sorcery", "new-horizons:havoc"}},
		{{"new-horizons:chaos", "new-horizons:havoc"}},
		{{"new-horizons:shadow", "new-horizons:sorcery"}},
		{{"new-horizons:havoc", "new-horizons:shadow"}},
		{{"new-horizons:chaos", "new-horizons:nature"}},
		{{"new-horizons:nature", "new-horizons:shadow"}},
		{{"new-horizons:havoc", "new-horizons:nature"}}
	}};
	for(size_t index = 0; index < factions.size(); ++index)
	{
		const auto preferred = newHorizonsMagic::preferredSchools(rules, factions[index]);
		ASSERT_EQ(preferred.size(), 2u);
		EXPECT_EQ(preferred.front().serializationKey(), expectedPreferences[index][0]);
		EXPECT_EQ(preferred.back().serializationKey(), expectedPreferences[index][1]);
	}

	auto historical = rules;
	historical.Struct().erase("mageGuildGeneration");
	for(auto & [name, faction] : historical["factions"].Struct())
	{
		(void)name;
		faction["major"] = faction["preferredA"];
		faction["minor"] = faction["preferredB"];
		faction.Struct().erase("preferredA");
		faction.Struct().erase("preferredB");
	}
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(historical));
	EXPECT_FALSE(newHorizonsMagic::mageGuildGenerationActive(historical));
	for(int level = 1; level <= 5; ++level)
		EXPECT_EQ(newHorizonsMagic::mageGuildSpellsAtLevel(historical, level), 6 - level);
}

TEST(NewHorizonsMagicTest, FixedSchoolMageGuildGenerationRequiresCompleteUnweightedFactionPreferences)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["factions"].Struct().erase("core:castle");
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);

	rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	rules.Struct().erase("factions");
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);

	rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	rules["factionWeights"]["major"].Integer() = 3;
	rules["factionWeights"]["minor"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicTest, WisdomDiscountRoundsUpAfterListedMultiplier)
{
	EXPECT_EQ(newHorizonsMagic::wisdomAdjustedCost(5, 1, MasteryLevel::NONE), 5);
	EXPECT_EQ(newHorizonsMagic::wisdomAdjustedCost(5, 1, MasteryLevel::BASIC), 5);
	EXPECT_EQ(newHorizonsMagic::wisdomAdjustedCost(5, 3, MasteryLevel::BASIC), 14);
	EXPECT_EQ(newHorizonsMagic::wisdomAdjustedCost(10, 1, MasteryLevel::ADVANCED), 8);
	EXPECT_EQ(newHorizonsMagic::wisdomAdjustedCost(10, 1, MasteryLevel::EXPERT), 7);
	EXPECT_EQ(newHorizonsMagic::wisdomAdjustedCost(1, 1, MasteryLevel::EXPERT), 1);
	EXPECT_EQ(newHorizonsMagic::wisdomAdjustedCost(0, 1, MasteryLevel::EXPERT), 1);
	EXPECT_THROW(newHorizonsMagic::wisdomAdjustedCost(5, 0, MasteryLevel::BASIC), std::runtime_error);
}

TEST(NewHorizonsMagicTest, UnsupportedSnapshotShapeAndVersionFailClosed)
{
	JsonNode rules(JsonMap{});
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	rules["schemaVersion"].Float() = 1.5;
	rules["rulesetVersion"].Integer() = 1;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = 2;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	rules["rulesetVersion"].Integer() = 1;
	rules["pretendSupported"].Bool() = true;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST(NewHorizonsMagicTest, SchoolsUseScopedCurrentSerializationAndReadLegacyNumeric)
{
	for(auto school : {SpellSchool::ANY, SpellSchool::AIR, SpellSchool::FIRE, SpellSchool::WATER, SpellSchool::EARTH})
	{
		CMemorySerializer current;
		current.oser & school;
		std::string encoded;
		current.iser & encoded;
		EXPECT_EQ(encoded, school.serializationKey());
		EXPECT_EQ(SpellSchool::fromSerializationKey(encoded), school);

		CMemorySerializer legacy;
		legacy.oser.version = ESerializationVersion::HERO_COMMANDS;
		legacy.iser.version = ESerializationVersion::HERO_COMMANDS;
		const auto before = school;
		legacy.oser & school;
		SpellSchool restored;
		legacy.iser & restored;
		EXPECT_EQ(restored, school);
		EXPECT_EQ(school, before);
	}
	EXPECT_THROW(SpellSchool::fromSerializationKey("air"), std::runtime_error);
	EXPECT_THROW(SpellSchool::fromSerializationKey("missing-required-module:air"), std::runtime_error);
}
