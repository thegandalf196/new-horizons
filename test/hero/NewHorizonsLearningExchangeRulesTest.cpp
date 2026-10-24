/*
 * NewHorizonsLearningExchangeRulesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/entities/hero/NewHorizonsLearningExchangeSpecialties.h"
using namespace newHorizonsHeroes;
namespace
{
JsonNode exchangeRules()
{
	auto rules = testHeroRules();
	auto & table = rules["learningExchangeSpecialties"];
	table["version"].Integer() = 1;
	for(const auto key : {"ecumenicalScholar", "occultScholar"})
	{
		table[key]["learningPercentPerLevel"].Integer() = 5;
		table[key]["learningMaximumPercent"].Integer() = 100;
	}
	table["ecumenicalScholar"]["additionalSpellsTaught"].Integer() = 1;
	table["occultScholar"]["additionalSpellsReceived"].Integer() = 1;
	return rules;
}
}
TEST(NewHorizonsLearningExchangeRules, CaptureCopiesIndependentRowsAndKeepsHistoricalAbsence)
{
	auto source = exchangeRules();
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	source["learningExchangeSpecialties"]["ecumenicalScholar"]["learningMaximumPercent"].Integer() = 75;
	EXPECT_EQ(learningExchangeSpecialtyRules(captured, LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR)->learningMaximumPercent, 100);
	source["learningExchangeSpecialties"].Struct().erase("occultScholar");
	const auto independent = resolveHeroRules(source, HeroClassID(0));
	EXPECT_TRUE(learningExchangeSpecialtyRules(independent, LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR));
	EXPECT_FALSE(learningExchangeSpecialtyRules(independent, LearningExchangeSpecialtyKind::OCCULT_SCHOLAR));
	auto historical = captured;
	historical.Struct().erase("learningExchangeSpecialties");
	const auto before = historical;
	EXPECT_NO_THROW(validateResolvedHeroRules(historical));
	EXPECT_FALSE(learningExchangeSpecialtyRules(historical, LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR));
	EXPECT_EQ(historical, before);
}
TEST(NewHorizonsLearningExchangeRules, StrictRowsRejectMissingNullUnknownFractionAndUnsafeBounds)
{
	const auto valid = resolveHeroRules(exchangeRules(), HeroClassID(0));
	std::vector<JsonNode> bad(12, valid);
	bad[0]["learningExchangeSpecialties"] = JsonNode();
	bad[1]["learningExchangeSpecialties"]["version"].Integer() = 2;
	bad[2]["learningExchangeSpecialties"]["ecumenicalScholar"] = JsonNode();
	bad[3]["learningExchangeSpecialties"]["occultScholar"].Struct().erase("learningMaximumPercent");
	bad[4]["learningExchangeSpecialties"]["ecumenicalScholar"]["learningPercentPerLevel"].Integer() = 1001;
	bad[5]["learningExchangeSpecialties"]["occultScholar"]["learningMaximumPercent"].Integer() = 201;
	bad[6]["learningExchangeSpecialties"]["ecumenicalScholar"]["additionalSpellsTaught"].Integer() = 2;
	bad[7]["learningExchangeSpecialties"]["occultScholar"]["additionalSpellsReceived"].Integer() = -1;
	bad[8]["learningExchangeSpecialties"]["ecumenicalScholar"]["learningPercentPerLevel"].Float() = 0.5;
	bad[9]["learningExchangeSpecialties"]["occultScholar"]["learningMaximumPercent"].Bool() = true;
	bad[10]["learningExchangeSpecialties"]["ecumenicalScholar"]["additionalSpellsReceived"].Integer() = 1;
	bad[11]["learningExchangeSpecialties"]["grantScholar"].Bool() = true;
	for(const auto & value : bad)
	{
		EXPECT_THROW(validateResolvedHeroRules(value), std::runtime_error);
		EXPECT_THROW(learningExchangeSpecialtyRules(value, LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR), std::runtime_error);
	}
}
TEST(NewHorizonsLearningExchangeRules, VersionOnlyAndExplicitZeroStayPresentWithoutInventingRows)
{
	auto source = exchangeRules();
	source["learningExchangeSpecialties"].Struct().erase("occultScholar");
	auto & row = source["learningExchangeSpecialties"]["ecumenicalScholar"];
	row["learningPercentPerLevel"].Integer() = 0;
	row["learningMaximumPercent"].Integer() = 0;
	row["additionalSpellsTaught"].Integer() = 0;
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	const auto rule = learningExchangeSpecialtyRules(captured, LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR);
	ASSERT_TRUE(rule);
	EXPECT_EQ(rule->learningPercentPerLevel, 0);
	EXPECT_EQ(rule->additionalSpellsTaught, 0);
	source["learningExchangeSpecialties"].Struct().erase("ecumenicalScholar");
	const auto versionOnly = resolveHeroRules(source, HeroClassID(0));
	EXPECT_TRUE(versionOnly.Struct().contains("learningExchangeSpecialties"));
	EXPECT_FALSE(learningExchangeSpecialtyRules(versionOnly, LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR));
	EXPECT_FALSE(learningExchangeSpecialtyRules(versionOnly, LearningExchangeSpecialtyKind::OCCULT_SCHOLAR));
}
TEST(NewHorizonsLearningExchangeRules, UnsupportedCapabilityRejectsAnyPresentBlockButAllowsHistoricalAbsence)
{
	auto source = exchangeRules();
	EXPECT_THROW(validateLearningExchangeSpecialtiesSerialization(source, false), std::runtime_error);
	source["learningExchangeSpecialties"].Struct().erase("ecumenicalScholar");
	source["learningExchangeSpecialties"].Struct().erase("occultScholar");
	EXPECT_THROW(validateLearningExchangeSpecialtiesSerialization(source, false), std::runtime_error);
	source.Struct().erase("learningExchangeSpecialties");
	EXPECT_NO_THROW(validateLearningExchangeSpecialtiesSerialization(source, false));
}
