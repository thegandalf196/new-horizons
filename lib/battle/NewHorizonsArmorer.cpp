/*
 * NewHorizonsArmorer.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsArmorer.h"

#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsDiscipline.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsArmorer
{
bool hasLastStand(const CGHeroInstance * hero)
{
	if(!hero)
		return false;

	try
	{
		return hero->getPerkSkillRank(SKILL_ID) >= 3
			&& hero->hasActivePerk(SKILL_ID, LAST_STAND_PERK_ID);
	}
	catch(const std::exception &)
	{
		return false;
	}
}

bool canTriggerLastStand(const CGHeroInstance * hero, const battle::Unit * target)
{
	return hasLastStand(hero) && target && target->alive() && !target->isGhost()
		&& !target->isClone() && target->getPhantomInitialIntegrity() <= 0
		&& target->unitSlot() != SlotID::WAR_MACHINES_SLOT
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(target);
}

bool isEligiblePhysicalAttack(const battle::Unit * attacker, bool physicalDamage, bool spellLike)
{
	return physicalDamage && !spellLike
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attacker)
		&& attacker->unitSlot() != SlotID::WAR_MACHINES_SLOT;
}

DefendStance buildDefendStance(const battle::Unit * stack, bool holdFastApplies)
{
	DefendStance result;
	if(!stack)
		return result;

	const Bonus percentDefense(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 20, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE),
		BonusValueType::PERCENT_TO_ALL);
	const Bonus stanceDefense(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, stack->valOfBonuses(BonusType::DEFENSIVE_STANCE), BonusSourceID(),
		BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE);
	const Bonus weakCreatureDefense(BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE),
		BonusValueType::ADDITIVE_VALUE);
	const Bonus defendingTag(BonusDuration::STACK_GETS_TURN, BonusType::UNIT_DEFENDING,
		BonusSource::OTHER, 0, BonusSourceID());

	BonusList defense = *stack->getBonuses(
		Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)));
	const int oldDefense = defense.totalValue();
	defense.push_back(std::make_shared<Bonus>(percentDefense));
	defense.push_back(std::make_shared<Bonus>(stanceDefense));
	result.defenseIncrease = defense.totalValue() - oldDefense;
	const bool weakCreatureFallback = result.defenseIncrease == 0;
	if(weakCreatureFallback)
	{
		result.defenseIncrease = 1;
		result.bonuses.push_back(weakCreatureDefense);
	}
	else
		result.bonuses.push_back(percentDefense);

	const auto rangeBonus = [&](bool ranged)
	{
		const auto range = ranged
			? Selector::effectRange()(BonusLimitEffect::NO_LIMIT)
				.Or(Selector::effectRange()(BonusLimitEffect::ONLY_DISTANCE_FIGHT))
			: Selector::effectRange()(BonusLimitEffect::NO_LIMIT)
				.Or(Selector::effectRange()(BonusLimitEffect::ONLY_MELEE_FIGHT));
		BonusList projected = *stack->getBonuses(
			Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)).And(range));
		const int oldValue = projected.totalValue();
		projected.push_back(std::make_shared<Bonus>(percentDefense));
		projected.push_back(std::make_shared<Bonus>(stanceDefense));
		if(weakCreatureFallback)
			projected.push_back(std::make_shared<Bonus>(weakCreatureDefense));
		return std::max(0, projected.totalValue() - oldValue);
	};
	result.meleeDefenseBonus = rangeBonus(false);
	result.rangedDefenseBonus = rangeBonus(true);
	result.bonuses.push_back(stanceDefense);
	result.bonuses.push_back(defendingTag);
	if(holdFastApplies && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack)
		&& !stack->hasBonus(newHorizonsDiscipline::holdFastMoraleFloorBonusSelector()))
	{
		result.holdFastBonus = newHorizonsDiscipline::holdFastMoraleFloorBonus();
		result.holdFastApplied = true;
	}
	return result;
}

ArmorerLastStandDamageResult resolveLastStandDamage(int64_t incomingDamage,
	int64_t guardianSpiritHitPoints, int32_t guardianSpiritRounds,
	int64_t totalAvailableHealth, bool eligible, bool sideAlreadyUsed)
{
	ArmorerLastStandDamageResult result{incomingDamage, false};
	if(!eligible || sideAlreadyUsed || incomingDamage <= 0 || totalAvailableHealth <= 0)
		return result;

	const int64_t activeGuardianBuffer = guardianSpiritRounds > 0
		? std::max<int64_t>(0, guardianSpiritHitPoints) : 0;
	const int64_t absorbed = std::min(incomingDamage, activeGuardianBuffer);
	const int64_t damageAfterGuardian = incomingDamage - absorbed;
	if(damageAfterGuardian < totalAvailableHealth)
		return result;

	// The lethal condition above guarantees this sum is <= incomingDamage, so it
	// cannot overflow int64_t. Preserve one total HP through the normal CHealth
	// path after the Guardian buffer has absorbed its ordinary share.
	result.damageToApply = absorbed + totalAvailableHealth - 1;
	result.triggered = true;
	return result;
}
}
