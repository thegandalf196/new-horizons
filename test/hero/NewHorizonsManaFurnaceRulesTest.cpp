/*
 * NewHorizonsManaFurnaceRulesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsAdditionalSpecialties.h"
#include "../../lib/serializer/CMemorySerializer.h"
using namespace newHorizonsHeroes;
namespace
{
JsonNode configuredFurnace()
{
	auto rules = testHeroRules();
	rules["additionalSpecialties"]["version"].Integer() = 1;
	rules["additionalSpecialties"]["manaFurnace"]["restorePointsPerLevel"].Integer() = 2;
	rules["additionalSpecialties"]["manaFurnace"]["maximumRestorePoints"].Integer() = 40;
	return rules;
}
}
TEST(NewHorizonsManaFurnaceRules, CapturesOwnOptionalRowAndDoesNotFillHistoricalAbsence)
{
	auto source = configuredFurnace();
	ASSERT_NO_THROW(validateHeroRules(source, true));
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	ASSERT_TRUE(manaFurnaceRules(captured));
	EXPECT_EQ(manaFurnaceRules(captured)->restorePointsPerLevel, 2);
	EXPECT_EQ(manaFurnaceRules(captured)->maximumRestorePoints, 40);
	EXPECT_FALSE(fieldPhysicianRules(captured));
	source["additionalSpecialties"]["manaFurnace"]["restorePointsPerLevel"].Integer() = 9;
	EXPECT_EQ(manaFurnaceRules(captured)->restorePointsPerLevel, 2);
	auto old = captured;
	old["additionalSpecialties"].Struct().erase("manaFurnace");
	const auto before = old;
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_FALSE(manaFurnaceRules(old));
	EXPECT_EQ(old, before);
}
TEST(NewHorizonsManaFurnaceRules, StrictRowRejectsMalformedUnsafeAndPrerequisiteBypassConfiguration)
{
	const auto valid = resolveHeroRules(configuredFurnace(), HeroClassID(0));
	std::vector<JsonNode> malformed(6, valid);
	malformed[0]["additionalSpecialties"]["manaFurnace"] = JsonNode();
	malformed[1]["additionalSpecialties"]["manaFurnace"].Struct().erase("restorePointsPerLevel");
	malformed[2]["additionalSpecialties"]["manaFurnace"]["restorePointsPerLevel"].Float() = 0.5;
	malformed[3]["additionalSpecialties"]["manaFurnace"]["restorePointsPerLevel"].Integer() = -1;
	malformed[4]["additionalSpecialties"]["manaFurnace"]["maximumRestorePoints"].Integer() = 1000001;
	malformed[5]["additionalSpecialties"]["manaFurnace"]["requiresActiveIntelligence"].Bool() = false;
	for(const auto & rules : malformed)
	{
		EXPECT_THROW(validateResolvedHeroRules(rules), std::runtime_error);
		EXPECT_THROW(manaFurnaceRules(rules), std::runtime_error);
	}
	auto maximum = valid;
	maximum["additionalSpecialties"]["manaFurnace"]["restorePointsPerLevel"].Integer() = 1000;
	maximum["additionalSpecialties"]["manaFurnace"]["maximumRestorePoints"].Integer() = 1000000;
	EXPECT_NO_THROW(validateResolvedHeroRules(maximum));
}
TEST(NewHorizonsManaFurnaceRules, ExistingAdditionalCapabilityProtectsOptionalPolicy)
{
	const auto rules = configuredFurnace();
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer previous;
	previous.oser.version = ESerializationVersion::NEW_HORIZONS_SCHOOL_SPELLCRAFT_SPECIALTIES;
	EXPECT_THROW(source.serialize(previous.oser), std::runtime_error);
	EXPECT_TRUE(previous.extractBuffer().empty());
	CMemorySerializer current;
	source.serialize(current.oser);
	GameSettings loaded;
	loaded.serialize(current.iser);
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
}
