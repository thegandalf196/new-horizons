/*
 * NewHorizonsCombatSkills.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsCombatSkills.h"

#include "../mapObjects/CGHeroInstance.h"
#include "CUnitState.h"
#include "Unit.h"
#include "../bonuses/Bonus.h"

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
bool hasLuckyRecovery(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(LUCK_SKILL_ID),
		std::string(LUCKY_RECOVERY_PERK_ID));
}

int64_t luckyRecoveryAmount(int64_t actualDamage, bool genericRecovery, bool sylvanRecovery)
{
	const int contributions = static_cast<int>(genericRecovery) + static_cast<int>(sylvanRecovery);
	const int64_t damage = std::max<int64_t>(0, actualDamage);
	// Do not multiply an unbounded HP amount before dividing. With one source
	// this is precisely the existing Sylvan floor(actualDamage / 10).
	return damage / 10 * contributions + damage % 10 * contributions / 10;
}

Bonus gamblerLuckPenalty()
{
	const auto skill = SecondarySkill(SecondarySkill::decode(std::string(LUCK_SKILL_ID)));
	Bonus bonus(BonusDuration::STACK_GETS_TURN, BonusType::LUCK,
		BonusSource::SECONDARY_SKILL, GAMBLER_LUCK_PENALTY, BonusSourceID(skill));
	bonus.stacking = std::string(GAMBLER_PERK_ID);
	bonus.description.appendRawString("Gambler: -2 Luck until the next Creature Activation");
	return bonus;
}

bool isGamblerLuckPenalty(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::STACK_GETS_TURN
		&& bonus->type == BonusType::LUCK && bonus->val == GAMBLER_LUCK_PENALTY
		&& bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->stacking == GAMBLER_PERK_ID;
}

int armorerRank(const CGHeroInstance * hero)
{
	return rank(hero, "new-horizons:armorer");
}

int armorerReductionPercent(int value)
{
	return std::clamp(value, 0, 3) * 5;
}

int armorerReductionPercent(const CGHeroInstance * hero)
{
	if(!hero)
		return 0;
	const int coreReduction = armorerReductionPercent(armorerRank(hero));
	const int specialtyPercent = hero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARMORER));
	return coreReduction * (100 + specialtyPercent) / 100;
}

int formationFightingReductionPercent(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(ARMORER_SKILL_ID), std::string(FORMATION_FIGHTING_PERK_ID))
		? FORMATION_FIGHTING_REDUCTION_PERCENT : 0;
}

bool isOrdinaryCreatureAttacker(const battle::Unit * attacker)
{
	return attacker && !attacker->isTurret()
		&& !attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& attacker->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER;
}

bool isPhysicalCreatureAttack(const battle::Unit * attacker, bool physicalDamage)
{
	return physicalDamage && isOrdinaryCreatureAttacker(attacker)
		&& attacker->unitSlot() != SlotID::WAR_MACHINES_SLOT;
}

bool isPhysicalCreatureLuckAttack(const battle::Unit * attacker, bool physicalDamage)
{
	return isPhysicalCreatureAttack(attacker, physicalDamage);
}

int paviseReductionPercent(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(ARMORER_SKILL_ID), std::string(PAVISE_PERK_ID))
		? PAVISE_REDUCTION_PERCENT : 0;
}

std::int64_t applyVeteran(battle::CUnitState * state, const CGHeroInstance * hero)
{
	if(!state)
		return 0;

	const auto damageSinceActivation = state->veteranPhysicalDamageSinceActivation;
	state->veteranPhysicalDamageSinceActivation = 0;
	if(damageSinceActivation <= 0 || !hero
		|| !hero->hasActivePerk(std::string(ARMORER_SKILL_ID), std::string(VETERAN_PERK_ID))
		|| !state->alive() || state->isTimeStopped() || state->isClone()
		|| state->getPhantomInitialIntegrity() > 0)
		return 0;

	const std::int64_t healingRequest = (damageSinceActivation / 100) * VETERAN_RECOVERY_PERCENT
		+ (damageSinceActivation % 100) * VETERAN_RECOVERY_PERCENT / 100;
	if(healingRequest <= 0)
		return 0;

	auto healing = healingRequest;
	return state->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;
}

int archeryRank(const CGHeroInstance * hero)
{
	return rank(hero, "new-horizons:archery");
}

int archeryDamagePercent(int value)
{
	return std::clamp(value, 0, 3) * 10;
}

int archeryDamagePercent(const CGHeroInstance * hero)
{
	const int coreDamagePercent = archeryDamagePercent(archeryRank(hero));
	if(!hero)
		return coreDamagePercent;

	const int specialtyPercent = hero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::ARCHERY));
	return coreDamagePercent * (100 + specialtyPercent) / 100;
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
