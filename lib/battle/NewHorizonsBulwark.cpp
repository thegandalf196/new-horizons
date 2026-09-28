/*
 * NewHorizonsBulwark.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsBulwark.h"

#include "CUnitState.h"
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

bool hasThickHide(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(THICK_HIDE_ID));
}

bool hasBogAmbush(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(BOG_AMBUSH_ID));
}

bool hasDeepBulwark(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(DEEP_BULWARK_ID));
}

bool hasSwampRenewal(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(SWAMP_RENEWAL_ID));
}

bool hasMireGrip(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(MIRE_GRIP_ID));
}

bool hasToxicSpines(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(TOXIC_SPINES_ID));
}

bool hasSharedCover(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(SHARED_COVER_ID));
}

bool hasImmovable(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(IMMOVABLE_ID));
}

bool hasVengefulMire(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(VENGEFUL_MIRE_ID));
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
	return preemptivePercent(value, false);
}

int preemptivePercent(int value, bool bogAmbush)
{
	int result = 0;
	switch(value)
	{
		case 1: result = 50; break;
		case 2: result = 75; break;
		case 3: result = 100; break;
		default: return 0;
	}
	return bogAmbush ? std::min(100, result + 25) : result;
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

int reflectionBasisPoints(int value, bool ranged, bool thickHide)
{
	return reflectionBasisPoints(value, ranged, thickHide, false);
}

int reflectionBasisPoints(int value, bool ranged, bool thickHide, bool vengefulMire)
{
	const int meleePercent = reflectionPercent(value);
	if(meleePercent == 0 || (ranged && !thickHide))
		return 0;

	const int basisPoints = meleePercent * BASIS_POINTS_PER_PERCENT * (ranged ? 50 : 100) / 100;
	return ranged || !vengefulMire
		? basisPoints
		: std::min(7500, basisPoints + 25 * BASIS_POINTS_PER_PERCENT);
}

int sharedCoverBasisPoints(int value)
{
	return std::max(0, value) / 2;
}

int64_t applySwampRenewal(battle::CUnitState * stack, const CGHeroInstance * hero)
{
	if(!stack || !stack->alive() || stack->bulwarkDefendPhysicalDamage <= 0 || !hasSwampRenewal(hero))
		return 0;

	int64_t healing = stack->bulwarkDefendPhysicalDamage / 10;
	stack->bulwarkDefendPhysicalDamage = 0;
	stack->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	return healing;
}

int64_t reflectedDamage(int64_t actualHealthLoss, int reflectionBasisPoints)
{
	if(actualHealthLoss <= 0 || reflectionBasisPoints <= 0)
		return 0;

	const int64_t boundedBasisPoints = std::clamp(reflectionBasisPoints, 0, 10000);
	const int64_t wholeUnits = actualHealthLoss / 10000;
	const int64_t remainingBasisPoints = actualHealthLoss % 10000;
	return wholeUnits * boundedBasisPoints + remainingBasisPoints * boundedBasisPoints / 10000;
}

int64_t toxicSpinesPoisonBase(int64_t actualReflectedHealthLoss)
{
	if(actualReflectedHealthLoss <= 0)
		return 0;
	return std::max<int64_t>(1, actualReflectedHealthLoss / 4);
}

bool applyPhysicalPoison(battle::CUnitState * target, int64_t baseDamage, int32_t sourceStackId)
{
	if(!target || baseDamage <= 0 || baseDamage < target->physicalPoisonBaseDamage)
		return false;
	target->physicalPoisonBaseDamage = baseDamage;
	target->physicalPoisonActivationsRemaining = 3;
	target->physicalPoisonSourceStackId = sourceStackId;
	return true;
}

int64_t physicalPoisonTickDamage(const battle::CUnitState * target)
{
	if(!target || target->physicalPoisonBaseDamage <= 0
		|| target->physicalPoisonActivationsRemaining <= 0)
		return 0;
	const int activationIndex = 3 - target->physicalPoisonActivationsRemaining;
	const int64_t base = target->physicalPoisonBaseDamage;
	if(activationIndex == 1)
		return base > std::numeric_limits<int64_t>::max() - base / 2
			? std::numeric_limits<int64_t>::max() : base + base / 2;
	if(activationIndex >= 2)
		return base > std::numeric_limits<int64_t>::max() / 2
		? std::numeric_limits<int64_t>::max() : base * 2;
	return base;
}

void advancePhysicalPoison(battle::CUnitState * target)
{
	if(!target || target->physicalPoisonActivationsRemaining <= 0)
		return;
	--target->physicalPoisonActivationsRemaining;
	if(target->physicalPoisonActivationsRemaining == 0)
		clearPhysicalPoison(target);
}

void clearPhysicalPoison(battle::CUnitState * target)
{
	if(!target)
		return;
	target->physicalPoisonBaseDamage = 0;
	target->physicalPoisonActivationsRemaining = 0;
	target->physicalPoisonSourceStackId = -1;
}
}
