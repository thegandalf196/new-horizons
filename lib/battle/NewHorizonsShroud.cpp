/*
 * NewHorizonsShroud.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsShroud.h"

#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsShroud
{
int rank(const CGHeroInstance * hero)
{
	if(!hero)
		return 0;
	try
	{
		return std::clamp(hero->getPerkSkillRank(std::string(SKILL_ID)), 0, 3);
	}
	catch(const std::exception &)
	{
		return 0;
	}
}

int flankingDamagePercent(int value)
{
	switch(value)
	{
		case 1: return 25;
		case 2: return 40;
		case 3: return 60;
		default: return 0;
	}
}

bool deniesRetaliation(int value)
{
	return value >= 3;
}
}
