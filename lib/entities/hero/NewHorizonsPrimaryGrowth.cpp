/*
 * NewHorizonsPrimaryGrowth.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsPrimaryGrowth.h"

namespace newHorizonsHeroes
{
std::array<int, GameConstants::PRIMARY_SKILLS> calculatePrimaryGrowth(
	const PrimaryProfile & profile,
	std::span<const ExtraPrimaryRoll> opportunities,
	std::span<const int> draws)
{
	profile.baseAtLevel(1); // Validate the supplied profile before calculating.
	auto gains = profile.growth;
	if(profile.progressionVersion != PRIMARY_PROFILE_VERSION_TWENTY_POINT)
		return gains;
	if(opportunities.size() != draws.size())
		throw std::invalid_argument("Each primary growth opportunity requires one draw");
	for(size_t i = 0; i < opportunities.size(); ++i)
	{
		const auto attribute = opportunities[i].attribute.getNum();
		const int chance = opportunities[i].chancePercent;
		if(attribute < 0 || attribute >= GameConstants::PRIMARY_SKILLS
			|| chance < 0 || chance > 100 || draws[i] < 0 || draws[i] >= 100)
			throw std::invalid_argument("Invalid primary growth opportunity or draw");
		if(draws[i] < chance)
		{
			if(gains[attribute] == std::numeric_limits<int>::max())
				throw std::overflow_error("Primary growth bonus overflow");
			++gains[attribute];
		}
	}
	return gains;
}
}
