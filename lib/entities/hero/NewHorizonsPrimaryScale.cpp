/*
 * NewHorizonsPrimaryScale.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsPrimaryScale.h"

namespace newHorizonsHeroes
{
int64_t scaledPowerEffect(int fixedValue, int perPower, int powerRating, int divisor)
{
	if(fixedValue < 0 || perPower < 0 || powerRating < 0 || divisor <= 0)
		throw std::runtime_error("Invalid primary spell-power scale input");
	return fixedValue + static_cast<int64_t>(perPower) * powerRating / divisor;
}

int64_t manaFromKnowledge(int knowledge, int multiplierPercent)
{
	if(knowledge < 0 || multiplierPercent < 0)
		throw std::runtime_error("Invalid knowledge/mana input");
	return static_cast<int64_t>(knowledge) * multiplierPercent / 100;
}
}
