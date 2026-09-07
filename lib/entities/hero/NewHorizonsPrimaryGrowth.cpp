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
	if(opportunities.size() != draws.size())
		throw std::runtime_error("Each primary growth opportunity requires its own draw");
	auto gains = profile.growth;
	for(size_t i = 0; i < opportunities.size(); ++i)
	{
		const auto & opportunity = opportunities[i];
		const auto index = opportunity.attribute.getNum();
		if(index < 0 || index >= GameConstants::PRIMARY_SKILLS
			|| opportunity.chancePercent < 0 || opportunity.chancePercent > 100
			|| draws[i] < 0 || draws[i] >= 100)
			throw std::runtime_error("Invalid primary growth opportunity or draw");
		if(draws[i] < opportunity.chancePercent)
		{
			if(gains[index] == std::numeric_limits<int>::max())
				throw std::runtime_error("Primary growth overflow");
			++gains[index];
		}
	}
	return gains;
}
}
