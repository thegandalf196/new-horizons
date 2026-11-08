/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeDamageFactors.h"
#include "NativeDamageFacts.h"
#include "IDamageCalculatorScript.h"

#include <cmath>
#include <stdexcept>

namespace nativeDamage
{
namespace
{
double defenseIgnored(const DamageAttackInfo & info, double reduction, double defense, bool targetDefense)
{
	reduction /= 100;
	double ignored = reduction > 0 ? std::floor(reduction * defense) + 1 : 0;
	if(targetDefense && info.shooting && info.luckyStrike)
		ignored += std::floor(static_cast<double>(info.luckyRangedDefenseIgnorePercent) * defense / 100);
	if(targetDefense && info.shooting && info.physicalDamage)
	{
		ignored += std::floor(static_cast<double>(info.rangedDefenseIgnoreBasisPoints) * std::max(0.0, defense) / 10000);
		ignored += std::floor(static_cast<double>(info.archeryRangedDefenseIgnorePercent) * std::max(0.0, defense) / 100);
		ignored += std::floor(static_cast<double>(info.warMachinesPiercingBoltsDefenseIgnorePercent) * std::max(0.0, defense) / 100);
	}
	if(targetDefense && !info.shooting && info.physicalDamage && info.coordinatedMeleeDefenseIgnorePercent > 0)
		ignored += std::floor(static_cast<double>(info.coordinatedMeleeDefenseIgnorePercent) * std::max(0.0, defense) / 100);
	if(targetDefense && !info.shooting && info.physicalDamage && info.stormWingsDefenseIgnorePercent > 0)
		ignored += std::floor(static_cast<double>(info.stormWingsDefenseIgnorePercent) * std::max(0.0, defense) / 100);
	if(targetDefense && !info.shooting)
	{
		ignored += std::floor(static_cast<double>(info.chargeDefenseIgnorePercent) * defense / 100);
		ignored += std::floor(static_cast<double>(info.meleeDefenseIgnorePercent) * defense / 100);
	}
	if(targetDefense)
		ignored += std::floor(static_cast<double>(info.arcaneInspirationDefenseIgnorePercent) * std::max(0.0, defense) / 100);
	return -std::min(ignored, defense);
}

double attackDefenseFactor(const DamageAttackInfo & info, const Facts & facts)
{
	const auto & attacker = facts.attacker;
	const auto & defender = facts.defender;
	double frenzy = attacker.value(BonusType::IN_FRENZY);
	if(frenzy != 0)
		frenzy = std::trunc(frenzy * (attacker.defense
			+ defenseIgnored(info, facts.defenderDefenseReduction, attacker.defense, false)) / 100);
	const double slayer = defender.has(BonusType::KING) && facts.hasSlayer
		&& facts.slayerMastery >= defender.value(BonusType::KING) ? facts.slayerValue : 0;
	double ignoredAttack = 0;
	if(defender.has(BonusType::ENEMY_ATTACK_REDUCTION) && facts.defenderAttackReduction > 0)
		ignoredAttack = std::max(0.0, std::floor(attacker.attack * (100 - facts.defenderAttackReduction) / 100))
			- attacker.attack;
	const double attack = attacker.attack + frenzy + slayer + ignoredAttack;
	double defense = 0;
	if(!defender.has(BonusType::IN_FRENZY))
	{
		defense = defender.defense;
		if(!info.shooting)
		{
			const double ignoredStance = std::floor(static_cast<double>(info.defensiveStanceDefenseBonus)
				* std::max(0, std::min(100, info.defensiveStanceDamageReductionIgnorePercent)) / 100);
			defense = std::max(0.0, defense - ignoredStance);
		}
		defense += defenseIgnored(info, facts.attackerDefenseReduction, defense, true);
	}
	const double advantage = attack - defense;
	if(advantage > 0) return std::min(info.attackFactorPerPoint * advantage, info.attackFactorCap);
	if(advantage < 0) return -std::min(info.defenseFactorPerPoint * -advantage, info.defenseFactorCap);
	return 0;
}

double rangeFactor(const DamageAttackInfo & info, const Facts & facts)
{
	if(info.shooting)
	{
		if(info.archeryAdjacentRangedTarget)
		{
			if(info.archeryIgnoreAdjacentRangedPenalty)
			{
				if(info.pointBlankPreservesAdjacentExemption) return 0;
				return info.targetedRangedCommand ? -0.25 : 0;
			}
			return info.targetedRangedCommand ? -0.25 : -0.5;
		}
		if(facts.distancePenalty)
			return info.targetedRangedCommand || info.archeryHighArc ? -0.25 : -0.5;
		return 0;
	}
	return !facts.attacker.has(BonusType::NO_MELEE_PENALTY) && facts.attacker.shooter ? -0.5 : 0;
}
}

std::pair<double, double> baseDamage(const DamageAttackInfo & info, const Facts & facts)
{
	const auto & attacker = facts.attacker;
	double low = attacker.minDamage;
	double high = attacker.maxDamage;
	if(low > high)
	{
		std::ostringstream error;
		error << "creature " << attacker.creatureKey << " has minimal damage (" << low
			<< ") greater than maximal damage (" << high << ")";
		throw std::runtime_error(error.str());
	}
	if(attacker.has(BonusType::SIEGE_WEAPON) && !attacker.turret && info.machineBaseDamage <= 0)
	{
		const double multiplier = info.siegeSkillMultiplier > 0 ? info.siegeSkillMultiplier
			: facts.heroArtifactAttack + facts.heroBaseAttack + 1;
		low *= multiplier;
		high *= multiplier;
	}
	const double shift = attacker.value(BonusType::ALWAYS_MAXIMUM_DAMAGE) - attacker.value(BonusType::ALWAYS_MINIMUM_DAMAGE);
	low = std::max(1.0, low + shift);
	high = std::max(1.0, high + shift);
	const bool cursed = attacker.has(BonusType::ALWAYS_MINIMUM_DAMAGE);
	const bool blessed = attacker.has(BonusType::ALWAYS_MAXIMUM_DAMAGE);
	if(!(cursed && blessed))
	{
		if(info.archeryMaximumCreatureDamage) low = high;
		else if(cursed) high = low;
		else if(blessed) low = high;
	}
	low *= attacker.count;
	high *= attacker.count;
	if(attacker.has(BonusType::SIEGE_WEAPON) && info.machineBaseDamage > 0)
		low = high = info.machineBaseDamage;
	return {low, high};
}

std::array<double, 24> factors(const DamageAttackInfo & info, const Facts & facts)
{
	const auto & attacker = facts.attacker;
	const auto & defender = facts.defender;
	std::array<double, 24> result{};
	result[0] = attackDefenseFactor(info, facts);
	result[1] = (facts.damageBoost + (info.shooting ? info.targetedRangedCommandPercent : 0)
		+ info.executionerDamagePercent + info.heroOrderDamagePercent + info.combinedArmsDamagePercent
		+ info.bloodrageDamagePercent + info.elementalAttunementDamagePercent + info.shroudFlankingDamagePercent + info.shroudDeepFlankDamagePercent
		+ info.newHorizonsArcheryDamagePercent + info.battlecraftWaitDamagePercent
		+ info.relentlessAssaultDamagePercent + info.archeryCrossfireDamagePercent) / 100;
	result[2] = attacker.value(BonusType::GENERAL_DAMAGE_PREMY) / 100;
	if(info.luckyStrike && !info.newHorizonsFinalLuck && info.sylvanLuckyStrikeTotalPercent <= 0)
		result[3] = 1.0 + attacker.value(BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE) / 100;
	if(info.chargeDistance > 0 && attacker.has(BonusType::JOUSTING) && !defender.has(BonusType::CHARGE_IMMUNITY))
		result[4] = info.chargeDistance * attacker.value(BonusType::JOUSTING) / 100;
	result[5] = info.deathBlow ? 1.0 : 0;
	result[6] = info.doubleDamage ? facts.doubleDamageBonus / 100 : 0;
	result[7] = attacker.has(BonusType::HATE) ? facts.hatedCreature / 100 : 0;
	const auto protections = legacyPhysicalProtectionFactors(info, facts.physicalProtection);
	std::copy(protections.begin(), protections.end(), result.begin() + 8);
	result[13] = rangeFactor(info, facts);
	if(info.shooting && !info.archeryIgnoreObstaclePenalty && !info.archeryHighArc && facts.wallPenalty)
		result[14] = info.targetedRangedCommand ? -0.25 : -0.5;
	result[15] = -facts.attackerAttackReduction / 100;
	result[16] = info.unluckyStrike ? -0.5 : 0;
	if(info.shooting && attacker.has(BonusType::FORGETFULL))
		result[17] = -std::min(attacker.value(BonusType::FORGETFULL), 100.0) / 100;
	result[18] = defender.has(BonusType::GENERAL_DAMAGE_REDUCTION) ? -facts.petrification / 100 : 0;
	if(defender.value(BonusType::LEVEL_SPELL_IMMUNITY) >= 5 && attacker.creatureKey == "core:magicElemental")
		result[19] = -0.5;
	if(defender.has(BonusType::MIND_IMMUNITY) && attacker.creatureKey == "core:psychicElemental")
		result[20] = -0.5;
	if(defender.value(BonusType::VULNERABLE_FROM_BACK) != 0 && facts.toReverse)
		result[21] = defender.value(BonusType::VULNERABLE_FROM_BACK) / 100;
	result[22] = attacker.has(BonusType::HATES_TRAIT) ? facts.hatedTraits / 100 : 0;
	if(attacker.has(BonusType::REVENGE))
		result[23] = std::sqrt((attacker.baseAmount + 1) * attacker.maxHealth
			/ (attacker.availableHealth + attacker.maxHealth) - 1);
	return result;
}
}
