/*
 * NewHorizonsArchery.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsArchery.h"

#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsArchery
{
bool hasPerk(const CGHeroInstance * hero, std::string_view perk)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(perk));
}

bool hasTargetCaller(const CGHeroInstance * hero)
{
	return hasPerk(hero, TARGET_CALLER);
}

bool hasSkirmisher(const CGHeroInstance * hero)
{
	return hasPerk(hero, SKIRMISHER);
}

bool hasPointBlankShot(const CGHeroInstance * hero)
{
	return hasPerk(hero, POINT_BLANK_SHOT);
}

bool hasCounterfire(const CGHeroInstance * hero)
{
	return hasPerk(hero, COUNTERFIRE);
}

bool canUseSkirmisher(const CGHeroInstance * hero, const battle::Unit * shooter)
{
	return hasSkirmisher(hero) && shooter && shooter->alive() && shooter->isShooter()
		&& shooter->canShoot() && !shooter->isTurret()
		&& !shooter->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& shooter->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}

bool canUseCounterfire(const CGHeroInstance * hero, const battle::Unit * shooter)
{
	// Counterfire is a physical reaction, not a way to act while a stack is
	// removed from the battle by Time Stop, incapacitation, or Stone Gaze.
	return hasCounterfire(hero) && shooter && shooter->alive() && shooter->isShooter()
		&& shooter->canShoot() && shooter->canMove() && !shooter->isFrozen()
		&& !shooter->isTurret() && !shooter->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& shooter->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}
}
