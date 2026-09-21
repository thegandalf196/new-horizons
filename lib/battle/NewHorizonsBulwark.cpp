/*
 * NewHorizonsBulwark.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsBulwark.h"

#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsBulwark
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
		// Core-only and legacy profiles do not register this faction Skill.
		return 0;
	}
}

bool hasMireborn(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(MIREBORN_ID));
}

int reductionBasisPoints(int value, int heroDefense, bool mireTerrain)
{
	const int64_t defense = std::max(0, heroDefense);
	int64_t result = 0;
	switch(value)
	{
		case 1:
			result = 500 + 10 * defense;
			break;
		case 2:
			result = 750 + 15 * defense;
			break;
		case 3:
			result = 1000 + 20 * defense;
			break;
		default:
			return 0;
	}
	result += mireTerrain ? 500 : 0;
	return static_cast<int>(std::min<int64_t>(result, std::numeric_limits<int>::max()));
}

int preemptivePercent(int value)
{
	switch(value)
	{
		case 1: return 50;
		case 2: return 75;
		case 3: return 100;
		default: return 0;
	}
}

int reflectionPercent(int value)
{
	switch(value)
	{
		case 2: return 25;
		case 3: return 50;
		default: return 0;
	}
}
}
