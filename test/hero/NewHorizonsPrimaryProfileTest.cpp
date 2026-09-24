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
JsonNode profile(std::array<int, 4> starting, std::array<int, 4> growth,
	std::optional<int> progressionVersion = std::nullopt)
{
	JsonNode result;
	if(progressionVersion)
		result["progressionVersion"].Integer() = *progressionVersion;
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
	EXPECT_EQ(parsed.progressionVersion, newHorizonsHeroes::PRIMARY_PROFILE_VERSION_LEGACY);
	EXPECT_EQ(parsed.baseAtLevel(1), (std::array<int64_t, 4>{15, 20, 5, 10}));
	EXPECT_EQ(parsed.baseAtLevel(2), (std::array<int64_t, 4>{18, 24, 6, 12}));
	EXPECT_EQ(parsed.baseAtLevel(10), (std::array<int64_t, 4>{42, 56, 14, 28}));
	EXPECT_EQ(parsed.baseAtLevel(20), expected);
	EXPECT_EQ(parsed.baseAtLevel(20), parsed.baseAtLevel(20));
	const auto legacyWithUnrelatedStart = newHorizonsHeroes::parsePrimaryProfile(profile({100, 0, 0, 0}, {3, 4, 1, 2}));
	EXPECT_EQ(legacyWithUnrelatedStart.baseAtLevel(1), (std::array<int64_t, 4>{15, 20, 5, 10}));
	const auto explicitLegacy = newHorizonsHeroes::parsePrimaryProfile(
		profile({15, 20, 5, 10}, {3, 4, 1, 2}, newHorizonsHeroes::PRIMARY_PROFILE_VERSION_LEGACY));
	EXPECT_EQ(explicitLegacy.baseAtLevel(2), parsed.baseAtLevel(2));
}

TEST(NewHorizonsPrimaryProfileTest, VersionTwoUsesAuthoredStartAndEighteenPointGrowth)
{
	const auto parsed = newHorizonsHeroes::parsePrimaryProfile(
		profile({30, 45, 10, 15}, {6, 7, 2, 3}, newHorizonsHeroes::PRIMARY_PROFILE_VERSION_STARTING_AND_GROWTH));
	EXPECT_EQ(parsed.progressionVersion, newHorizonsHeroes::PRIMARY_PROFILE_VERSION_STARTING_AND_GROWTH);
	EXPECT_EQ(parsed.baseAtLevel(1), (std::array<int64_t, 4>{30, 45, 10, 15}));
	EXPECT_EQ(parsed.baseAtLevel(2), (std::array<int64_t, 4>{36, 52, 12, 18}));
	EXPECT_EQ(parsed.baseAtLevel(10), (std::array<int64_t, 4>{84, 108, 28, 42}));
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
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(profile({30, 45, 10, 15}, {6, 7, 2, 2}, 2)), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(profile({29, 45, 10, 15}, {6, 7, 2, 3}, 2)), std::runtime_error);
	const int maximumRating = std::numeric_limits<int>::max();
	// This invalid total is 100 modulo 32 bits and must not pass a narrow sum.
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(
		profile({maximumRating, maximumRating, 51, 51}, {5, 5, 4, 4}, 2)), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(profile({30, 45, 10, 15}, {6, 7, 2, 3}, 3)), std::runtime_error);
	auto fractionalVersion = profile({30, 45, 10, 15}, {6, 7, 2, 3}, 2);
	fractionalVersion["progressionVersion"].Float() = 1.5;
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(fractionalVersion), std::runtime_error);
	auto oversizedVersion = profile({30, 45, 10, 15}, {6, 7, 2, 3}, 2);
	oversizedVersion["progressionVersion"].Float() = 1e100;
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(oversizedVersion), std::runtime_error);
	auto fractional = profile({15, 20, 5, 10}, {3, 4, 1, 2});
	fractional["growth"].Vector()[0].Float() = 3.5;
	EXPECT_THROW(newHorizonsHeroes::parsePrimaryProfile(fractional), std::runtime_error);
	const auto parsed = newHorizonsHeroes::parsePrimaryProfile(profile({15, 20, 5, 10}, {3, 4, 1, 2}));
	EXPECT_THROW(parsed.baseAtLevel(0), std::runtime_error);
	const int maximumLevel = std::numeric_limits<int>::max();
	const auto maximumLegacyLevel = parsed.baseAtLevel(maximumLevel);
	EXPECT_EQ(maximumLegacyLevel[1], static_cast<int64_t>(4) * (static_cast<int64_t>(maximumLevel) + 4));
	EXPECT_GT(maximumLegacyLevel[1], std::numeric_limits<int>::max());
	const auto parsedV2 = newHorizonsHeroes::parsePrimaryProfile(
		profile({30, 45, 10, 15}, {6, 7, 2, 3}, newHorizonsHeroes::PRIMARY_PROFILE_VERSION_STARTING_AND_GROWTH));
	const auto maximumV2Level = parsedV2.baseAtLevel(maximumLevel);
	EXPECT_EQ(maximumV2Level[0], 30 + static_cast<int64_t>(6) * (static_cast<int64_t>(maximumLevel) - 1));
}
