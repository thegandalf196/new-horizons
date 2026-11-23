/*
 * NewHorizonsMovementLevelGrowthTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/json/JsonUtils.h"
#include <limits>

namespace
{
JsonNode movementProfile()
{
	auto rules = testHeroRules();
	rules["dailyMovement"]["version"].Integer() = 1;
	rules["dailyMovement"]["pointsPerLevel"].Integer() = 1;
	return rules;
}
}

using NewHorizonsMovementLevelGrowth = vcmiTestData::OriginalDataTest;

TEST_F(NewHorizonsMovementLevelGrowth, ApprovedDailyBudgetsAndWideLevelArithmetic)
{
	const auto rules = movementProfile();
	for(const auto [level, expected] : {std::pair{1u, 200}, {10u, 209}, {20u, 219}, {30u, 229}})
		EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(0,
			newHorizonsMovement::levelMovementBonus(rules, level)), expected);
	EXPECT_EQ(newHorizonsMovement::levelMovementBonus(rules, 0), 0);
	EXPECT_EQ(newHorizonsMovement::levelMovementBonus(rules, std::numeric_limits<uint32_t>::max()),
		static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) - 1);
	EXPECT_EQ(newHorizonsMovement::maximumDailyMovement(0,
		newHorizonsMovement::levelMovementBonus(rules, std::numeric_limits<uint32_t>::max())),
		std::numeric_limits<int>::max());
}

TEST_F(NewHorizonsMovementLevelGrowth, CapturedPolicyIsIndependentAndHistoricalAbsenceStaysAbsent)
{
	auto rules = movementProfile();
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsHeroes", "daily Movement policy"));
	ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(rules, true));
	const auto captured = newHorizonsHeroes::resolveHeroRules(rules, HeroClassID(0));
	EXPECT_NO_THROW(newHorizonsHeroes::validateResolvedHeroRules(captured));
	rules["dailyMovement"]["pointsPerLevel"].Integer() = 2;
	EXPECT_EQ(newHorizonsMovement::levelMovementBonus(captured, 20), 19);
	EXPECT_EQ(newHorizonsMovement::levelMovementBonus(
		newHorizonsHeroes::resolveHeroRules(rules, HeroClassID(0)), 20), 38);
	rules.Struct().erase("dailyMovement");
	const auto historical = newHorizonsHeroes::resolveHeroRules(rules, HeroClassID(0));
	EXPECT_FALSE(historical.Struct().contains("dailyMovement"));
	EXPECT_EQ(newHorizonsMovement::levelMovementBonus(historical, 20), 0);
}

TEST_F(NewHorizonsMovementLevelGrowth, PolicyRejectsMalformedOrUnsupportedData)
{
	for(const int points : {-1, 1001})
	{
		auto rules = movementProfile();
		rules["dailyMovement"]["pointsPerLevel"].Integer() = points;
		EXPECT_THROW(newHorizonsMovement::validateDailyMovementRules(rules), std::runtime_error);
	}
	auto rules = movementProfile();
	rules["dailyMovement"]["pointsPerLevel"].Float() = 1.5;
	EXPECT_THROW(newHorizonsMovement::validateDailyMovementRules(rules), std::runtime_error);
	rules = movementProfile();
	rules["dailyMovement"]["version"].Integer() = 2;
	EXPECT_THROW(newHorizonsMovement::validateDailyMovementRules(rules), std::runtime_error);
	rules = movementProfile();
	rules["dailyMovement"]["unknown"].Bool() = true;
	EXPECT_THROW(newHorizonsMovement::validateDailyMovementRules(rules), std::runtime_error);
	rules = movementProfile();
	rules["dailyMovement"].Struct().erase("pointsPerLevel");
	EXPECT_THROW(newHorizonsMovement::validateDailyMovementRules(rules), std::runtime_error);
	rules = movementProfile();
	rules["dailyMovement"]["pointsPerLevel"].Integer() = 0;
	EXPECT_EQ(newHorizonsMovement::levelMovementBonus(rules, 20), 0);
}
