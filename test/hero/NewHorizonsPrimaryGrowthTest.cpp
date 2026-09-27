/*
 * NewHorizonsPrimaryGrowthTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPrimaryGrowth.h"

using newHorizonsHeroes::ExtraPrimaryRoll;
using newHorizonsHeroes::PrimaryProfile;
using newHorizonsHeroes::calculatePrimaryGrowth;

TEST(NewHorizonsPrimaryGrowthTest, CanonicalSkillRollsAreIndependentAndUseStrictPercentageBoundary)
{
	const PrimaryProfile profile{{30, 45, 10, 15}, {6, 9, 2, 3},
		newHorizonsHeroes::PRIMARY_PROFILE_VERSION_TWENTY_POINT};
	const std::array<ExtraPrimaryRoll, 4> opportunities = {{
		{PrimarySkill::ATTACK, 10}, {PrimarySkill::ATTACK, 20},
		{PrimarySkill::DEFENSE, 30}, {PrimarySkill::KNOWLEDGE, 0}
	}};
	const std::array<int, 4> draws{9, 19, 30, 0};
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, draws), (std::array<int, 4>{8, 9, 2, 3}));
	EXPECT_EQ(calculatePrimaryGrowth(profile, {}, {}), profile.growth);
	EXPECT_THROW(calculatePrimaryGrowth(profile, opportunities, {}), std::invalid_argument);
	const std::array<int, 4> invalid{9, 19, 100, 0};
	EXPECT_THROW(calculatePrimaryGrowth(profile, opportunities, invalid), std::invalid_argument);
	EXPECT_EQ(profile.growth, (std::array<int, 4>{6, 9, 2, 3}));
}

TEST(NewHorizonsPrimaryGrowthTest, EveryPercentileHasTheExpectedCanonicalOutcome)
{
	const PrimaryProfile profile{{30, 45, 10, 15}, {6, 9, 2, 3},
		newHorizonsHeroes::PRIMARY_PROFILE_VERSION_TWENTY_POINT};
	for(int chance : {0, 10, 20, 30, 100})
	{
		const std::array<ExtraPrimaryRoll, 1> opportunities{{{PrimarySkill::ATTACK, chance}}};
		int successes = 0;
		for(int percentile = 0; percentile < 100; ++percentile)
		{
			const std::array<int, 1> draws{percentile};
			const auto gains = calculatePrimaryGrowth(profile, opportunities, draws);
			successes += gains[0] - profile.growth[0];
			for(size_t attribute = 1; attribute < gains.size(); ++attribute)
				EXPECT_EQ(gains[attribute], profile.growth[attribute]);
		}
		EXPECT_EQ(successes, chance);
	}
}

TEST(NewHorizonsPrimaryGrowthTest, NoSkillOpportunityLeavesDeclaredTenPointGrowth)
{
	const PrimaryProfile profile{{15, 20, 5, 10}, {4, 4, 1, 1}};
	EXPECT_EQ(calculatePrimaryGrowth(profile, {}, {}), profile.growth);
}

TEST(NewHorizonsPrimaryGrowthTest, LegacySkillRowsDoNotAlterClassVector)
{
	const PrimaryProfile profile{{15, 20, 5, 10}, {4, 4, 1, 1}};
	const std::array<ExtraPrimaryRoll, 3> opportunities = {{
		{PrimarySkill::ATTACK, 50}, {PrimarySkill::DEFENSE, 50}, {PrimarySkill::SPELL_POWER, 50}
	}};
	const std::array<int, 3> draws{49, 49, 50};
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, draws), profile.growth);
	EXPECT_EQ(profile.growth, (std::array<int, 4>{4, 4, 1, 1}));
}

TEST(NewHorizonsPrimaryGrowthTest, LegacyRowsAndDrawsAreIgnored)
{
	const PrimaryProfile profile{{20, 15, 5, 10}, {5, 3, 1, 1}};
	const std::array<ExtraPrimaryRoll, 3> opportunities = {{
		{PrimarySkill::KNOWLEDGE, 0}, {PrimarySkill::ATTACK, 100}, {PrimarySkill::ATTACK, 100}
	}};
	const std::array<int, 3> draws{0, 99, 0};
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, draws), profile.growth);
}

TEST(NewHorizonsPrimaryGrowthTest, LegacyRowsDoNotRequireChanceValidation)
{
	const PrimaryProfile profile{{20, 15, 5, 10}, {5, 3, 1, 1}};
	std::array<ExtraPrimaryRoll, 1> opportunities{{{PrimarySkill::ATTACK, 50}}};
	const std::array<int, 1> good{0};
	const std::array<int, 1> invalid{100};
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, {}), profile.growth);
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, invalid), profile.growth);
	opportunities[0].chancePercent = 101;
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, good), profile.growth);
	opportunities[0] = {PrimarySkill::NONE, 50};
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, good), profile.growth);
	EXPECT_EQ(profile.growth, (std::array<int, 4>{5, 3, 1, 1}));
}
