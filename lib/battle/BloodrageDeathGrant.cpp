/*
 * BloodrageDeathGrant.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsBloodrage.h"
#include <algorithm>

newHorizonsBloodrage::DeathGrant newHorizonsBloodrage::deathGrant(const CGHeroInstance * hero,
	bool firstDeath, bool eliteOrChampion, int rank, int cap, int currentPercent)
{
	const bool firstBlood = hasFirstBlood(hero);
	const int increments = deathIncrementCount(firstDeath, eliteOrChampion, firstBlood, hasSlayer(hero));
	return {std::min(cap, currentPercent + increments * incrementForRank(rank)), firstBlood};
}
