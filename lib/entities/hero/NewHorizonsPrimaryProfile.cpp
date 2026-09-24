/*
 * NewHorizonsPrimaryProfile.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsPrimaryProfile.h"
#include <cmath>

namespace newHorizonsHeroes
{
namespace
{
std::array<int, GameConstants::PRIMARY_SKILLS> readRatings(const JsonNode & node, int minimum, int maximum)
{
	if(!node.isVector() || node.Vector().size() != GameConstants::PRIMARY_SKILLS)
		throw std::runtime_error("Primary profile requires four ratings in Attack/Defense/Power/Knowledge order");
	std::array<int, GameConstants::PRIMARY_SKILLS> result;
	for(size_t i = 0; i < result.size(); ++i)
	{
		const auto & value = node.Vector()[i];
		if(!value.isNumber() || !std::isfinite(value.Float()) || value.Float() < minimum
			|| value.Float() > maximum || std::floor(value.Float()) != value.Float())
			throw std::runtime_error("Invalid primary profile rating");
		result[i] = static_cast<int>(value.Integer());
	}
	return result;
}

int64_t ratingTotal(const std::array<int, GameConstants::PRIMARY_SKILLS> & ratings)
{
	return std::accumulate(ratings.begin(), ratings.end(), int64_t{0});
}

void validateProfile(const PrimaryProfile & profile)
{
	if(std::any_of(profile.starting.begin(), profile.starting.end(), [](int value) { return value < 0; }))
		throw std::runtime_error("Invalid primary profile starting rating");

	int maximumGrowth = 0;
	int expectedGrowth = 0;
	switch(profile.progressionVersion)
	{
	case PRIMARY_PROFILE_VERSION_LEGACY:
		maximumGrowth = PRIMARY_GROWTH_PER_LEVEL;
		expectedGrowth = PRIMARY_GROWTH_PER_LEVEL;
		break;
	case PRIMARY_PROFILE_VERSION_STARTING_AND_GROWTH:
		if(ratingTotal(profile.starting) != PRIMARY_PROFILE_V2_STARTING_TOTAL)
			throw std::runtime_error("Version 2 primary starting ratings must total one hundred");
		maximumGrowth = PRIMARY_PROFILE_V2_GROWTH_PER_LEVEL;
		expectedGrowth = PRIMARY_PROFILE_V2_GROWTH_PER_LEVEL;
		break;
	default:
		throw std::runtime_error("Unsupported primary profile progression version");
	}

	if(std::any_of(profile.growth.begin(), profile.growth.end(), [maximumGrowth](int value)
		{ return value < 1 || value > maximumGrowth; })
		|| ratingTotal(profile.growth) != expectedGrowth)
		throw std::runtime_error("Invalid primary profile growth total");
}
}

PrimaryProfile parsePrimaryProfile(const JsonNode & data)
{
	if(!data.isStruct())
		throw std::runtime_error("Primary profile must be an object");
	for(const auto & [key, value] : data.Struct())
		if(key != "starting" && key != "growth" && key != "progressionVersion")
			throw std::runtime_error("Unknown primary profile field: " + key);

	PrimaryProfile result;
	const auto versionField = data.Struct().find("progressionVersion");
	if(versionField != data.Struct().end())
	{
		const auto & version = versionField->second;
		if(!version.isNumber() || !std::isfinite(version.Float()) || std::floor(version.Float()) != version.Float())
			throw std::runtime_error("Primary profile progressionVersion must be an integer");
		if(version.Float() != PRIMARY_PROFILE_VERSION_LEGACY
			&& version.Float() != PRIMARY_PROFILE_VERSION_STARTING_AND_GROWTH)
			throw std::runtime_error("Unsupported primary profile progression version");
		// The float has already been matched against the only supported small
		// integer values, so this conversion cannot overflow.
		result.progressionVersion = static_cast<int>(version.Float());
	}

	result.starting = readRatings(data["starting"], 0, std::numeric_limits<int>::max());
	const auto maximumGrowth = result.progressionVersion == PRIMARY_PROFILE_VERSION_LEGACY
		? PRIMARY_GROWTH_PER_LEVEL : PRIMARY_PROFILE_V2_GROWTH_PER_LEVEL;
	result.growth = readRatings(data["growth"], 1, maximumGrowth);
	validateProfile(result);
	return result;
}

std::array<int64_t, GameConstants::PRIMARY_SKILLS> PrimaryProfile::baseAtLevel(int level) const
{
	if(level < 1)
		throw std::runtime_error("Primary profile requires a positive hero level");
	validateProfile(*this);

	std::array<int64_t, GameConstants::PRIMARY_SKILLS> result;
	for(size_t i = 0; i < result.size(); ++i)
	{
		if(progressionVersion == PRIMARY_PROFILE_VERSION_LEGACY)
			// Preserve historical snapshots exactly: level L has g * (L + 4).
			result[i] = static_cast<int64_t>(growth[i]) * (static_cast<int64_t>(level) + 4);
		else
			// The accepted replacement profile begins at its authored values and
			// adds one fixed class vector for each subsequent level.
			result[i] = static_cast<int64_t>(starting[i])
				+ static_cast<int64_t>(growth[i]) * (static_cast<int64_t>(level) - 1);
	}
	return result;
}
}
