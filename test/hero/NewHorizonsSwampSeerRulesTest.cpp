/*
 * NewHorizonsSwampSeerRulesTest.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/entities/hero/NewHorizonsSwampSeer.h"
using namespace newHorizonsHeroes;
namespace
{
JsonNode seerRules()
{
	auto rules = testHeroRules();
	auto & row = rules["swampSeerSpecialty"];
	row["version"].Integer() = 1;
	row["learningPercentPerLevel"].Integer() = 5;
	row["learningMaximumPercent"].Integer() = 100;
	row["studyExperiencePerMagicPrimaryPoint"].Integer() = 250;
	return rules;
}
}
TEST(NewHorizonsSwampSeerRules, CapturedSingletonIsIndependentAndHistoricalAbsenceStaysAbsent)
{
	auto source = seerRules();
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	ASSERT_TRUE(swampSeerRules(captured));
	source["swampSeerSpecialty"]["studyExperiencePerMagicPrimaryPoint"].Integer() = 999;
	EXPECT_EQ(swampSeerRules(captured)->studyExperiencePerMagicPrimaryPoint, 250);
	auto old = captured;
	old.Struct().erase("swampSeerSpecialty");
	const auto before = old;
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_FALSE(swampSeerRules(old));
	EXPECT_EQ(old, before);
}
TEST(NewHorizonsSwampSeerRules, StrictRequiredFieldsRejectVersionOnlyNullUnknownFractionAndBounds)
{
	const auto valid = resolveHeroRules(seerRules(), HeroClassID(0));
	std::vector<JsonNode> bad(10, valid);
	bad[0]["swampSeerSpecialty"] = JsonNode();
	bad[1]["swampSeerSpecialty"]["version"].Integer() = 2;
	bad[2]["swampSeerSpecialty"].Struct().erase("studyExperiencePerMagicPrimaryPoint");
	bad[3]["swampSeerSpecialty"]["learningPercentPerLevel"].Integer() = 1001;
	bad[4]["swampSeerSpecialty"]["learningMaximumPercent"].Integer() = 201;
	bad[5]["swampSeerSpecialty"]["studyExperiencePerMagicPrimaryPoint"].Integer() = 1000001;
	bad[6]["swampSeerSpecialty"]["studyExperiencePerMagicPrimaryPoint"].Integer() = -1;
	bad[7]["swampSeerSpecialty"]["learningPercentPerLevel"].Float() = 0.5;
	bad[8]["swampSeerSpecialty"]["learningMaximumPercent"].Bool() = true;
	bad[9]["swampSeerSpecialty"]["grantHistorian"].Bool() = true;
	for(const auto & value : bad)
		EXPECT_THROW(validateResolvedHeroRules(value), std::runtime_error);
	JsonNode versionOnly;
	versionOnly["swampSeerSpecialty"]["version"].Integer() = 1;
	EXPECT_THROW(validateSwampSeerSerialization(versionOnly, true), std::runtime_error);
}
TEST(NewHorizonsSwampSeerRules, ExplicitZeroRemainsCapturedAndUnsupportedAnyPresenceRejects)
{
	auto source = seerRules();
	auto & row = source["swampSeerSpecialty"];
	row["learningPercentPerLevel"].Integer() = 0;
	row["learningMaximumPercent"].Integer() = 0;
	row["studyExperiencePerMagicPrimaryPoint"].Integer() = 0;
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	ASSERT_TRUE(swampSeerRules(captured));
	EXPECT_EQ(swampSeerRules(captured)->studyExperiencePerMagicPrimaryPoint, 0);
	EXPECT_THROW(validateSwampSeerSerialization(captured, false), std::runtime_error);
	JsonNode versionOnly;
	versionOnly["swampSeerSpecialty"]["version"].Integer() = 1;
	EXPECT_THROW(validateSwampSeerSerialization(versionOnly, false), std::runtime_error);
	EXPECT_NO_THROW(validateSwampSeerSerialization(testHeroRules(), false));
}
