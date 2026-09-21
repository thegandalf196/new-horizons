/*
 * NewHorizonsBattlecraft.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsBattlecraft.h"

#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsBattlecraft
{
int rank(const CGHeroInstance * hero)
{
	if(!hero)
		return 0;
	try
	{
		return std::clamp(hero->getPerkSkillRank("new-horizons:battlecraft"), 0, 3);
	}
	catch(const std::exception &)
	{
		return 0;
	}
}

int rankPercent(int value)
{
	return std::clamp(value, 0, 3) * 5;
}

bool hasEntrench(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:battlecraft", "new-horizons:battlecraft.entrench");
}

int defendReductionPercent(const CGHeroInstance * hero)
{
	const int value = rankPercent(rank(hero));
	return value > 0 ? value + (hasEntrench(hero) ? 5 : 0) : 0;
}
}
