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

bool canGrantEarnedMorale(const battle::CUnitState & unit, const CGHeroInstance * hero)
{
	return hasPerk(hero) && unit.alive() && !unit.isGhost() && !unit.isTimeStopped()
		&& !unit.isTurret() && unit.unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
		&& !unit.hasBonusOfType(BonusType::SIEGE_WEAPON) && !unit.hadMorale;
}

bool grantEarnedMorale(battle::CUnitState & unit, const CGHeroInstance * hero)
{
	if(!canGrantEarnedMorale(unit, hero))
		return false;
	if(!unit.heroicSpiritRetaliation)
	{
		unit.heroicSpiritRetaliation = true;
		unit.heroicSpiritMoralePending = true;
	}
	return true;
}

void beginActivation(battle::CUnitState & unit, BattleUnitTurnReason reason)
{
	if(reason == BattleUnitTurnReason::MORALE && unit.heroicSpiritMoralePending)
	{
		unit.heroicSpiritMoralePending = false;
		return;
	}
	unit.heroicSpiritRetaliation = false;
	unit.heroicSpiritMoralePending = false;
}
}
