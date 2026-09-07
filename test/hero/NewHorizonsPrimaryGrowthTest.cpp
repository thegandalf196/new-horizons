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

TEST(NewHorizonsPrimaryGrowthTest, NoSkillOpportunityLeavesDeclaredTenPointGrowth)
{
	const PrimaryProfile profile{{15, 20, 5, 10}, {4, 4, 1, 1}};
	EXPECT_EQ(calculatePrimaryGrowth(profile, {}, {}), profile.growth);
}

TEST(NewHorizonsPrimaryGrowthTest, SeparateSkillSuccessesDoNotCompeteOrConsumeBaseGrowth)
{
	const PrimaryProfile profile{{15, 20, 5, 10}, {4, 4, 1, 1}};
	const std::array<ExtraPrimaryRoll, 3> opportunities = {{
		{PrimarySkill::ATTACK, 50}, {PrimarySkill::DEFENSE, 50}, {PrimarySkill::SPELL_POWER, 50}
	}};
	const std::array<int, 3> draws{49, 49, 50};
	const std::array<int, 4> expected{5, 5, 1, 1};
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, draws), expected);
	EXPECT_EQ(profile.growth, (std::array<int, 4>{4, 4, 1, 1}));
}

TEST(NewHorizonsPrimaryGrowthTest, ZeroCertainAndSharedAttributeOpportunitiesKeepTheirOwnDraws)
{
	const PrimaryProfile profile{{20, 15, 5, 10}, {5, 3, 1, 1}};
	const std::array<ExtraPrimaryRoll, 3> opportunities = {{
		{PrimarySkill::KNOWLEDGE, 0}, {PrimarySkill::ATTACK, 100}, {PrimarySkill::ATTACK, 100}
	}};
	const std::array<int, 3> draws{0, 99, 0};
	EXPECT_EQ(calculatePrimaryGrowth(profile, opportunities, draws), (std::array<int, 4>{7, 3, 1, 1}));
}

TEST(NewHorizonsPrimaryGrowthTest, InvalidInputFailsWithoutMutatingProfile)
{
	const PrimaryProfile profile{{20, 15, 5, 10}, {5, 3, 1, 1}};
	std::array<ExtraPrimaryRoll, 1> opportunities{{{PrimarySkill::ATTACK, 50}}};
	const std::array<int, 1> good{0};
	const std::array<int, 1> invalid{100};
	EXPECT_THROW(calculatePrimaryGrowth(profile, opportunities, {}), std::runtime_error);
	EXPECT_THROW(calculatePrimaryGrowth(profile, opportunities, invalid), std::runtime_error);
	opportunities[0].chancePercent = 101;
	EXPECT_THROW(calculatePrimaryGrowth(profile, opportunities, good), std::runtime_error);
	opportunities[0] = {PrimarySkill::NONE, 50};
	EXPECT_THROW(calculatePrimaryGrowth(profile, opportunities, good), std::runtime_error);
	EXPECT_EQ(profile.growth, (std::array<int, 4>{5, 3, 1, 1}));
}
