/*
 * NewHorizonsLeadership.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsLeadership.h"

#include <algorithm>
#include <stdexcept>

namespace newHorizonsHeroes
{
LeadershipCapacity leadershipCapacity(int classBase, int classPerLevel,
	int level, int skillBonusPercent, uint64_t used, int minimumMovementPercent)
{
	constexpr int maximumProfileValue = 1000000;
	constexpr int maximumSkillBonusPercent = 1000;
	if(classBase <= 0 || classBase > maximumProfileValue
		|| classPerLevel < 0 || classPerLevel > maximumProfileValue
		|| level <= 0 || level > maximumProfileValue
		|| skillBonusPercent < 0 || skillBonusPercent > maximumSkillBonusPercent
		|| minimumMovementPercent <= 0 || minimumMovementPercent > 100)
		throw std::invalid_argument("Invalid New Horizons leadership capacity inputs");

	const int64_t base = classBase + int64_t(level - 1) * classPerLevel;
	const int64_t capacity = base * (100 + skillBonusPercent) / 100;
	int movementPercent = 100;
	if(used > static_cast<uint64_t>(capacity))
		movementPercent = std::max(minimumMovementPercent,
			static_cast<int>(static_cast<uint64_t>(capacity) * 100 / used));
	return {capacity, used, movementPercent};
}

int leadershipMovement(int unscaledMovement, int movementPercent)
{
	if(unscaledMovement < 0 || movementPercent <= 0 || movementPercent > 100)
		throw std::invalid_argument("Invalid New Horizons leadership movement inputs");
	return static_cast<int>(int64_t(unscaledMovement) * movementPercent / 100);
}
}
