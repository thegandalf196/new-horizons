/*
 * NewHorizonsRegenerationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include <limits>

using namespace newHorizonsMagic;

namespace
{
JsonNode magicRulesForVersion(const int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	if(version < SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		rules.Struct().erase("schoolRankPowerCoefficientPercent");
	return rules;
}
}

TEST(NewHorizonsRegenerationTest, CanonicalNatureSpellAndHerbalistPerkAreActive)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(validateRules(rules));
	const auto & regeneration = rules["spells"][std::string(NATURE_REGENERATION_SPELL)];
	ASSERT_EQ(regeneration["schools"].Vector().size(), 1u);
	EXPECT_EQ(regeneration["schools"].Vector().front().String(), "new-horizons:nature");
	EXPECT_EQ(regeneration["level"].Integer(), 1);
	ASSERT_EQ(regeneration["costs"].Vector().size(), 4u);
	for(const auto & cost : regeneration["costs"].Vector())
		EXPECT_EQ(cost.Integer(), 4);

	const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
	const auto & naturePerks = perks["skills"][std::string(NATURE_MAGIC_SKILL)]["perks"].Vector();
	const auto herbalist = std::ranges::find_if(naturePerks, [](const JsonNode & perk)
	{
		return perk["id"].String() == NATURE_HERBALIST;
	});
	ASSERT_NE(herbalist, naturePerks.end());
	EXPECT_EQ((*herbalist)["effect"]["status"].String(), "active");
}

TEST(NewHorizonsRegenerationTest, SavedSchoolRankScalesOnlyTheSpellPowerTerm)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	EXPECT_EQ(regenerationRateMillionths(100, schoolRankPowerCoefficientPercent(rules, 0), false), 400'000);
	EXPECT_EQ(regenerationRateMillionths(100, schoolRankPowerCoefficientPercent(rules, 1), false), 422'500);
	EXPECT_EQ(regenerationRateMillionths(100, schoolRankPowerCoefficientPercent(rules, 2), false), 445'000);
	EXPECT_EQ(regenerationRateMillionths(100, schoolRankPowerCoefficientPercent(rules, 3), false), 467'500);
	EXPECT_EQ(regenerationRateMillionths(0, schoolRankPowerCoefficientPercent(rules, 3), false), 250'000)
		<< "the fixed base rate is not school-scaled";

	for(const int version : {RULESET_VERSION, DIRECT_DAMAGE_RULESET_VERSION})
	{
		const auto legacyRules = magicRulesForVersion(version);
		EXPECT_EQ(schoolRankPowerCoefficientPercent(legacyRules, 3), 100);
		EXPECT_EQ(regenerationRateMillionths(100, schoolRankPowerCoefficientPercent(legacyRules, 3), false), 400'000);
	}
}

TEST(NewHorizonsRegenerationTest, HerbalistAddsTenPointsBeforeTheFiftyPercentCap)
{
	EXPECT_EQ(regenerationRateMillionths(40, 115, false), 319'000);
	EXPECT_EQ(regenerationRateMillionths(40, 115, true), 419'000);
	EXPECT_EQ(regenerationRateMillionths(100, 100, true), REGENERATION_MAX_RATE_MILLIONTHS);
	EXPECT_EQ(regenerationRateMillionths(100, 145, true), REGENERATION_MAX_RATE_MILLIONTHS);
}

TEST(NewHorizonsRegenerationTest, FixedPointMarksAccumulateAndHealingIsClampedToSurvivingWounds)
{
	const int64_t oneFractionalHit = regenerationRateMillionths(100, 100, false);
	EXPECT_EQ(oneFractionalHit, 400'000);
	EXPECT_EQ(regenerationHealAmount(oneFractionalHit * 2, 10), 0);
	EXPECT_EQ(regenerationHealAmount(oneFractionalHit * 3, 10), 1);
	EXPECT_EQ(regenerationHealAmount(oneFractionalHit * 20, 2), 2);
	EXPECT_EQ(regenerationHealAmount(std::numeric_limits<int64_t>::max(), 0), 0);
	EXPECT_THROW(regenerationHealAmount(-1, 1), std::invalid_argument);
	EXPECT_THROW(regenerationHealAmount(1, -1), std::invalid_argument);
}

TEST(NewHorizonsRegenerationTest, RejectsImpossibleRateInputs)
{
	EXPECT_THROW(regenerationRateMillionths(-1, 100, false), std::invalid_argument);
	EXPECT_THROW(regenerationRateMillionths(100, -1, false), std::invalid_argument);
	EXPECT_THROW(regenerationRateMillionths(100, 1001, false), std::invalid_argument);
	EXPECT_THROW(regenerationRateMillionths(100, 100, false, -1), std::invalid_argument);
}
