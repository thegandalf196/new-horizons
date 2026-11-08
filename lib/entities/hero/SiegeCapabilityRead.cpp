/* Part of VCMI engine. GPL-2.0-or-later; see license.txt/AUTHORS. */
#include "StdInc.h"
#include "SiegeCapabilityRead.h"
#include "NewHorizonsCapabilityRules.h"
#include <stdexcept>

namespace newHorizonsHeroes
{
SiegeCapabilityRead::SiegeCapabilityRead(const JsonNode & rules, int artilleryRank)
{
	// Preserve complete context validation BEFORE the original artillery-rank check.
	multiplier = capabilityBallistaMultiplier(rules, artilleryRank);
	version = static_cast<int>(rules["rulesetVersion"].Integer());
	if(version >= 3)
		for(size_t index = 0; index < ratings.size(); ++index)
		{
			ratings[index] = static_cast<int>(rules["siege"]["siegeRating"].Vector()[index].Integer());
			controls[index] = static_cast<int>(rules["siege"]["directControlChance"].Vector()[index].Integer());
		}
}

int SiegeCapabilityRead::rankValue(const std::array<int, 4> & values, int rank, const char * key) const
{
	if(rank < 0 || rank > 3)
		throw std::runtime_error("Invalid New Horizons capability rules: skill rank outside NONE..EXPERT");
	if(version < 3)
		throw std::runtime_error(std::string("Invalid New Horizons capability rules: ") + key
			+ " requires capability ruleset v3");
	return values[rank];
}

int SiegeCapabilityRead::siegeRating(int rank) const
{
	return rankValue(ratings, rank, "siegeRating");
}

int SiegeCapabilityRead::directControlChance(int rank) const
{
	return rankValue(controls, rank, "directControlChance");
}
}
