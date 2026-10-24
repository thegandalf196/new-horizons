/*
 * NewHorizonsHerbalSurgeonRulesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/entities/hero/NewHorizonsHerbalSurgeon.h"
#include "../../lib/entities/hero/NewHorizonsRecoverySpecialties.h"
using namespace newHorizonsHeroes;
namespace
{
JsonNode herbalRules()
{
	auto rules = testHeroRules();
	rules["recoverySpecialties"]["version"].Integer() = 1;
	for(const auto key : {"tentPercentPerLevel", "regenerationPercentPerLevel"})
		rules["recoverySpecialties"]["herbalSurgeon"][key].Integer() = 5;
	for(const auto key : {"tentMaximumPercent", "regenerationMaximumPercent"})
		rules["recoverySpecialties"]["herbalSurgeon"][key].Integer() = 100;
	return rules;
}
}
TEST(NewHorizonsHerbalSurgeonRules, CapturePreservesIndependentChannelsAndAbsentHistoricalPolicy)
{
	auto source = herbalRules();
	source["recoverySpecialties"]["herbalSurgeon"]["tentMaximumPercent"].Integer() = 50;
	const auto captured = resolveHeroRules(source, HeroClassID(0));
	ASSERT_TRUE(herbalSurgeonRules(captured));
	EXPECT_EQ(herbalSurgeonRules(captured)->tentMaximumPercent, 50);
	EXPECT_EQ(herbalSurgeonRules(captured)->regenerationMaximumPercent, 100);
	source["recoverySpecialties"]["herbalSurgeon"]["tentMaximumPercent"].Integer() = 200;
	EXPECT_EQ(herbalSurgeonRules(captured)->tentMaximumPercent, 50);
	EXPECT_NO_THROW(validateRecoverySpecialtiesSerialization(source, true));
	EXPECT_THROW(validateRecoverySpecialtiesSerialization(captured, false), std::runtime_error);
	auto historical = captured;
	historical["recoverySpecialties"].Struct().erase("herbalSurgeon");
	const auto before = historical;
	EXPECT_FALSE(herbalSurgeonRules(historical));
	EXPECT_EQ(historical, before);
}
TEST(NewHorizonsHerbalSurgeonRules, RejectsUnsafeMissingUnknownAndBypassFields)
{
	const auto valid = resolveHeroRules(herbalRules(), HeroClassID(0));
	std::vector<JsonNode> invalid(5, valid);
	invalid[0]["recoverySpecialties"]["herbalSurgeon"] = JsonNode();
	invalid[1]["recoverySpecialties"]["herbalSurgeon"].Struct().erase("tentMaximumPercent");
	invalid[2]["recoverySpecialties"]["herbalSurgeon"]["regenerationMaximumPercent"].Integer() = 201;
	invalid[3]["recoverySpecialties"]["herbalSurgeon"]["tentPercentPerLevel"].Integer() = 1001;
	invalid[4]["recoverySpecialties"]["herbalSurgeon"]["grantHerbalist"].Bool() = true;
	for(const auto & rules : invalid)
		EXPECT_THROW(herbalSurgeonRules(rules), std::runtime_error);
}
