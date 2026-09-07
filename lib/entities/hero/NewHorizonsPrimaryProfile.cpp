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
}

PrimaryProfile parsePrimaryProfile(const JsonNode & data)
{
	if(!data.isStruct())
		throw std::runtime_error("Primary profile must be an object");
	for(const auto & [key, value] : data.Struct())
		if(key != "starting" && key != "growth")
			throw std::runtime_error("Unknown primary profile field: " + key);

	PrimaryProfile result;
	result.starting = readRatings(data["starting"], 0, std::numeric_limits<int>::max());
	result.growth = readRatings(data["growth"], 1, PRIMARY_GROWTH_PER_LEVEL);
	if(std::accumulate(result.growth.begin(), result.growth.end(), 0) != PRIMARY_GROWTH_PER_LEVEL)
		throw std::runtime_error("Primary growth must total ten points per level");
	return result;
}

std::array<int64_t, GameConstants::PRIMARY_SKILLS> PrimaryProfile::baseAtLevel(int level) const
{
	if(level < 1)
		throw std::runtime_error("Primary profile requires a positive hero level");
	if(std::any_of(starting.begin(), starting.end(), [](int value) { return value < 0; })
		|| std::any_of(growth.begin(), growth.end(), [](int value) { return value < 1 || value > PRIMARY_GROWTH_PER_LEVEL; })
		|| std::accumulate(growth.begin(), growth.end(), 0) != PRIMARY_GROWTH_PER_LEVEL)
		throw std::runtime_error("Invalid primary profile");
	std::array<int64_t, GameConstants::PRIMARY_SKILLS> result;
	for(size_t i = 0; i < result.size(); ++i)
		result[i] = static_cast<int64_t>(starting[i]) + static_cast<int64_t>(growth[i]) * (level - 1);
	return result;
}
}
