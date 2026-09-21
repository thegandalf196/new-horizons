/*
 * NewHorizonsCombatSkills.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsCombatSkills.h"

#include "../mapObjects/CGHeroInstance.h"

namespace
{
int rank(const CGHeroInstance * hero, const char * skill)
{
	if(!hero)
		return 0;
	try
	{
		return std::clamp(hero->getPerkSkillRank(skill), 0, 3);
	}
	catch(const std::exception &)
	{
		return 0;
	}
}
}

namespace newHorizonsCombatSkills
{
int armorerRank(const CGHeroInstance * hero)
{
	return rank(hero, "new-horizons:armorer");
}

int armorerReductionPercent(int value)
{
	return std::clamp(value, 0, 3) * 5;
}

int archeryRank(const CGHeroInstance * hero)
{
	return rank(hero, "new-horizons:archery");
}

int archeryDamagePercent(int value)
{
	return std::clamp(value, 0, 3) * 10;
}
}
