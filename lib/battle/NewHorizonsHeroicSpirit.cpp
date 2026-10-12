/*
 * NewHorizonsHeroicSpirit.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroicSpirit.h"
#include "CUnitState.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsHeroicSpirit
{
bool hasPerk(const CGHeroInstance * hero)
{
	return hero && hero->getPerkSkillRank(SKILL) >= MasteryLevel::EXPERT
		&& hero->hasActivePerk(SKILL, PERK);
}

bool canRollOrderMorale(const CGHeroInstance * hero, const HeroActionAllowanceState::Receipt & receipt)
{
	return HeroActionAllowanceState::isHeroicSpiritTrigger(receipt)
		&& hasPerk(hero) && hero->moraleVal() > 0;
}

bool canGrantEarnedMorale(const battle::CUnitState &, const CGHeroInstance *)
{
	return false;
}

bool grantEarnedMorale(battle::CUnitState &, const CGHeroInstance *)
{
	return false;
}

void beginActivation(battle::CUnitState & unit, BattleUnitTurnReason)
{
	unit.heroicSpiritRetaliation = false;
	unit.heroicSpiritMoralePending = false;
}
}
