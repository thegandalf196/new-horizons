/*
 * NewHorizonsSchoolSpellcraftRulesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/GameSettings.h"
#include "../../lib/serializer/CMemorySerializer.h"

using namespace newHorizonsHeroes;
namespace
{
JsonNode schoolRules()
{
	auto rules = testHeroRules();
	auto & block = rules["schoolSpellcraftSpecialties"];
	block["version"].Integer() = 1;
	for(const auto key : {"overcaster", "darkSavant", "primalFormula", "mireHexer"})
	{
		block[key]["percentPerLevel"].Integer() = 5;
		block[key]["maximumPercent"].Integer() = 100;
	}
	return rules;
}
}

TEST(NewHorizonsSchoolSpellcraftRules, CurrentCaptureIsIndependentAndAbsenceNeverFillsFromInstalledRules)
{
	auto configured = schoolRules();
	ASSERT_NO_THROW(validateHeroRules(configured, true));
	const auto captured = resolveHeroRules(configured, HeroClassID(0));
	const auto policy = schoolSpellcraftRules(captured, SchoolSpellcraftKind::OVERCASTER);
	ASSERT_TRUE(policy);
	EXPECT_EQ(policy->percentPerLevel, 5);
	EXPECT_EQ(policy->maximumPercent, 100);
	configured["schoolSpellcraftSpecialties"]["overcaster"]["percentPerLevel"].Integer() = 9;
	EXPECT_EQ(schoolSpellcraftRules(captured, SchoolSpellcraftKind::OVERCASTER)->percentPerLevel, 5);
	EXPECT_FALSE(individualSpecialtyRules(captured));
	auto historical = captured;
	historical.Struct().erase("schoolSpellcraftSpecialties");
	const auto before = historical;
	EXPECT_NO_THROW(validateResolvedHeroRules(historical));
	EXPECT_NO_THROW(validateIndividualSpecialtiesSerialization(historical, false, false, false));
	EXPECT_FALSE(schoolSpellcraftRules(historical, SchoolSpellcraftKind::DARK_SAVANT));
	EXPECT_EQ(historical, before);
}

TEST(NewHorizonsSchoolSpellcraftRules, StrictPresentPolicyRejectsMissingUnknownFractionalAndUnsafeBounds)
{
	const auto valid = resolveHeroRules(schoolRules(), HeroClassID(0));
	EXPECT_THROW(validateIndividualSpecialtiesSerialization(valid, true, true, false), std::runtime_error);
	std::vector<JsonNode> malformed(7, valid);
	malformed[0]["schoolSpellcraftSpecialties"] = JsonNode();
	malformed[1]["schoolSpellcraftSpecialties"]["version"].Integer() = 2;
	malformed[2]["schoolSpellcraftSpecialties"].Struct().erase("darkSavant");
	malformed[3]["schoolSpellcraftSpecialties"]["overcaster"]["percentPerLevel"].Float() = 0.5;
	malformed[4]["schoolSpellcraftSpecialties"]["primalFormula"]["maximumPercent"].Integer() = 201;
	malformed[5]["schoolSpellcraftSpecialties"]["mireHexer"]["unknown"].Integer() = 1;
	malformed[6]["schoolSpellcraftSpecialties"]["unknown"].Bool() = true;
	for(const auto & rules : malformed)
	{
		EXPECT_THROW(validateResolvedHeroRules(rules), std::runtime_error);
		EXPECT_THROW(schoolSpellcraftRules(rules, SchoolSpellcraftKind::OVERCASTER), std::runtime_error);
	}
	auto boundary = valid;
	boundary["schoolSpellcraftSpecialties"]["overcaster"]["maximumPercent"].Integer() = 200;
	EXPECT_NO_THROW(validateResolvedHeroRules(boundary));
}

TEST(NewHorizonsSchoolSpellcraftRules, GlobalCodecRejectsPreviousWriterBeforePrefixAndPreviousReaderBeforeInstalling)
{
	const auto rules = schoolRules();
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer previous;
	previous.oser.version = ESerializationVersion::NEW_HORIZONS_INDIVIDUAL_SPECIALTY_RIDERS;
	EXPECT_THROW(settings.serialize(previous.oser), std::runtime_error);
	EXPECT_TRUE(previous.extractBuffer().empty());
	CMemorySerializer current;
	ASSERT_NO_THROW(settings.serialize(current.oser));
	GameSettings loaded;
	ASSERT_NO_THROW(loaded.serialize(current.iser));
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
	JsonNode payload;
	payload["heroes"]["newHorizons"] = rules;
	CMemorySerializer incompatible;
	incompatible.oser & payload;
	incompatible.iser.version = ESerializationVersion::NEW_HORIZONS_INDIVIDUAL_SPECIALTY_RIDERS;
	const auto before = loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	EXPECT_THROW(loaded.serialize(incompatible.iser), std::runtime_error);
	EXPECT_EQ(loaded.getValue(EGameSettings::HEROES_NEW_HORIZONS), before);
}
