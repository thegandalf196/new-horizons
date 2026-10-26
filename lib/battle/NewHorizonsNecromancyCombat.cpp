/*
 * NewHorizonsNecromancyCombat.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsNecromancyCombat.h"
#include "BattleAttackInfo.h"
#include "BattleInfo.h"
#include "CUnitState.h"
#include "IBattleInfoCallback.h"
#include "LiveUnitEnvironmentContext.h"
#include "../CStack.h"
#include "../entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../spells/MagicalDamageReduction.h"

namespace newHorizonsNecromancy
{
CombatPassives captureCombatPassives(const CGHeroInstance * hero)
{
	const CreatureID skeleton(CreatureID::SKELETON);
	return {deathMarchSpeed(hero, skeleton), bonewardMagicalReductionPercent(hero, skeleton),
		tombGuardAdditionalRetaliations(hero, skeleton)};
}

CombatPassives passivesForCreature(const CombatPassives & captured, CreatureID creature)
{
	return isSkeletonFamily(creature) ? captured : CombatPassives{};
}

bool needsCombatPassiveSnapshot(const battle::Unit & unit)
{
	if(!isSkeletonFamily(unit.creatureId()))
		return false;
	const auto passives = unit.necromancyCombatPassives();
	return passives.movementSpeed > 0 || passives.magicalReductionPercent > 0
		|| passives.additionalRetaliations > 0;
}

int64_t adjustUnmitigatedMagicalDamage(const battle::Unit & target, int64_t rawDamage)
{
	const int reduction = target.intrinsicMagicalDamageReductionPercent();
	return reduction > 0 ? spells::calculateMagicalDamageReduction(rawDamage,
		std::vector<int>{reduction}, 0).damageWithoutPenetration : rawDamage;
}

void adjustCreatureDamage(const BattleAttackInfo & attack, DamageEstimation & damage)
{
	if(attack.physicalDamage || !attack.defender)
		return;
	const int reduction = attack.defender->intrinsicMagicalDamageReductionPercent();
	if(reduction <= 0)
		return;
	damage.damage.min = adjustUnmitigatedMagicalDamage(*attack.defender, damage.damage.min);
	damage.damage.max = adjustUnmitigatedMagicalDamage(*attack.defender, damage.damage.max);
	// Keep the existing script's casualty convention after the new mitigation.
	const auto killedBy = [&](int64_t amount) -> int64_t
	{
		const auto integrity = attack.defender->getPhantomIntegrity();
		if(integrity > 0)
			return amount >= integrity ? attack.defender->getCount() : 0;
		const auto firstHealth = attack.defender->getFirstHPleft();
		return amount < firstHealth ? 0 : std::min<int64_t>(attack.defender->getCount(),
			1 + (amount - firstHealth) / std::max<uint32_t>(1, attack.defender->getMaxHealth()));
	};
	damage.kills.min = killedBy(damage.damage.min);
	damage.kills.max = killedBy(damage.damage.max);
}
}

int32_t battle::CUnitState::intrinsicMagicalDamageReductionPercent() const
{
	return necromancyCombatPassives().magicalReductionPercent;
}

newHorizonsNecromancy::CombatPassives battle::CUnitState::necromancyCombatPassives() const
{
	return env ? env->unitNecromancyPassives(this) : newHorizonsNecromancy::CombatPassives{};
}

newHorizonsNecromancy::CombatPassives CStack::unitNecromancyPassives(const battle::Unit * unit) const
{
	if(!battle || !unit)
		return {};
	const battle::LiveUnitEnvironmentContext context(*battle);
	return battle::UnitEnvironmentQueries(context).unitNecromancyPassives(unit);
}
