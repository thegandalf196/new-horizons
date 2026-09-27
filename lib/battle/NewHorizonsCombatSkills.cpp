/*
 * NewHorizonsCombatSkills.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsCombatSkills.h"

#include "../mapObjects/CGHeroInstance.h"
#include "Unit.h"

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

bool isOrdinaryCreatureAttacker(const battle::Unit * attacker)
{
	return attacker && !attacker->isTurret()
		&& !attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& attacker->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}

int paviseReductionPercent(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(ARMORER_SKILL_ID), std::string(PAVISE_PERK_ID))
		? PAVISE_REDUCTION_PERCENT : 0;
}

int archeryRank(const CGHeroInstance * hero)
{
	return rank(hero, "new-horizons:archery");
}

int archeryDamagePercent(int value)
{
	return std::clamp(value, 0, 3) * 10;
}

int bracePreemptivePercent(int basePercent, bool hasCountercharge)
{
	if(!hasCountercharge || basePercent <= 0)
		return basePercent;
	return std::min(basePercent, 75) + 25;
}

int bracePreemptivePercent(int basePercent, const CGHeroInstance * hero)
{
	const bool hasCountercharge = hero && hero->hasActivePerk(
		std::string(ARMORER_SKILL_ID), std::string(COUNTERCHARGE_PERK_ID));
	return bracePreemptivePercent(basePercent, hasCountercharge);
}
}
