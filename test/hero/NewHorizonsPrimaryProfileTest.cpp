/*
 * NewHorizonsPrimaryProfileTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPrimaryProfile.h"

namespace
{
JsonNode profile(std::array<int, 4> starting, std::array<int, 4> growth)
{
	JsonNode result;
	for(auto value : starting)
		result["starting"].Vector().emplace_back(value);
	for(auto value : growth)
		result["growth"].Vector().emplace_back(value);
	return result;
}
}

TEST(NewHorizonsPrimaryProfileTest, UniversalPriorityGrowthMatchesDeclaredKnightExample)
{
	const auto parsed = newHorizonsHeroes::parsePrimaryProfile(profile({15, 20, 5, 10}, {3, 4, 1, 2}));
	const std::array<int64_t, 4> expected = {72, 96, 24, 48};
	EXPECT_EQ(parsed.baseAtLevel(20), expected);
	EXPECT_EQ(parsed.baseAtLevel(20), parsed.baseAtLevel(20));
}

TEST(NewHorizonsPrimaryProfileTest, ClassSpecificProfilesAlwaysAddTenWithoutRedistribution)
{
	for(const auto & growth : {std::array<int, 4>{4, 4, 1, 1}, {5, 3, 1, 1}, {5, 2, 2, 1}})
	{
		const auto parsed = newHorizonsHeroes::parsePrimaryProfile(profile({15, 20, 5, 10}, growth));
		const auto before = parsed.baseAtLevel(19);
		const auto after = parsed.baseAtLevel(20);
		int64_t total = 0;
		for(size_t i = 0; i < growth.size(); ++i)
		{
			EXPECT_EQ(after[i] - before[i], growth[i]);
			total += after[i] - before[i];
		}
		EXPECT_EQ(total, 10);
	}
}

TEST(NewHorizonsPrimaryProfileTest, MalformedProfilesAreRejectedAndLargeRatingsDoNotWrap)
{
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(profile({15, 20, 5, 10}, {4, 3, 2, 2})), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(profile({15, 20, 5, 10}, {5, 5, 0, 0})), std::runtime_error);
	auto fractional = profile({15, 20, 5, 10}, {3, 4, 1, 2});
	fractional["growth"].Vector()[0].Float() = 3.5;
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(fractional), std::runtime_error);
	const auto parsed = newHorizonsHeroes::parsePrimaryProfile(profile({15, 20, 5, 10}, {3, 4, 1, 2}));
	EXPECT_THROW(parsed.baseAtLevel(0), std::runtime_error);
	const auto maximumLevel = parsed.baseAtLevel(std::numeric_limits<int>::max());
	EXPECT_GT(maximumLevel[1], std::numeric_limits<int>::max());
}
