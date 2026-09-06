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
