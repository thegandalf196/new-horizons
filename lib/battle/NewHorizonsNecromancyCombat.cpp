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
#include "../bonuses/BonusList.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include "../mapObjects/army/CStackInstance.h"
#include "../entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../entities/hero/NewHorizonsNecromancy.h"
#include "../spells/MagicalDamageReduction.h"

namespace newHorizonsNecromancy
{
CombatPassives captureCombatPassives(const CGHeroInstance * hero)
{
	const CreatureID skeleton(CreatureID::SKELETON);
	return {deathMarchSpeed(hero, skeleton), bonewardMagicalReductionPercent(hero, skeleton),
		tombGuardAdditionalRetaliations(hero, skeleton), graveplatePhysicalReductionPercent(hero, skeleton),
		bonewardMagicResistancePercent(hero, skeleton), tombGuardDoubleStrike(hero, skeleton),
		deadMansLuckBonus(hero, skeleton)};
}

std::shared_ptr<const BonusList> appendLuckBonuses(const AFactionMember & member,
	std::shared_ptr<const BonusList> ordinary, int & additionalLuck)
{
	int luck = 0;
	if(const auto * unit = dynamic_cast<const battle::Unit *>(&member))
		luck = unit->necromancyCombatPassives().luckBonus;
	else if(const auto * stack = dynamic_cast<const CStackInstance *>(&member))
	{
		const auto * hero = dynamic_cast<const CGHeroInstance *>(stack->getArmy());
		if(!hero)
			if(const auto * town = dynamic_cast<const CGTownInstance *>(stack->getArmy()))
				hero = town->getGarrisonHero();
		luck = deadMansLuckBonus(hero, stack->getCreatureID());
	}
	additionalLuck = luck;
	if(luck == 0)
		return ordinary;
	auto result = std::make_shared<BonusList>(*ordinary);
	result->push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
		BonusSource::SECONDARY_SKILL, luck, BonusSourceID(SecondarySkill(SecondarySkill::decode(SKILL_ID)))));
	return result;
}

CombatPassives passivesForFamily(const CombatPassives & captured, bool family)
{
	return family ? captured : CombatPassives{};
}
CombatPassives passivesForCreature(const CombatPassives & captured, CreatureID creature)
{
	return passivesForFamily(captured, isSkeletonFamily(creature));
}

bool needsCombatPassiveSnapshot(const battle::Unit & unit)
{
	if(!isSkeletonFamily(unit.creatureId()))
		return false;
	const auto passives = unit.necromancyCombatPassives();
	return passives.movementSpeed > 0 || passives.magicalReductionPercent > 0
		|| passives.additionalRetaliations > 0 || passives.physicalReductionPercent > 0
		|| passives.magicResistancePercent > 0 || passives.doubleStrike || passives.luckBonus != 0;
}

int64_t adjustUnmitigatedMagicalDamage(const battle::Unit & target, int64_t rawDamage)
{
	const int reduction = target.intrinsicMagicalDamageReductionPercent();
	return reduction > 0 ? spells::calculateMagicalDamageReduction(rawDamage,
		std::vector<int>{reduction}, spells::targetMdrPenetrations(target)).damageWithPenetration : rawDamage;
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

int32_t battle::CUnitState::intrinsicPhysicalDamageReductionPercent() const
{
	return necromancyCombatPassives().physicalReductionPercent;
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
