/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeDamageCalculator.h"
#include "NativeDamageFacts.h"
#include "NativeDamageFactors.h"

#include <cmath>

namespace nativeDamage
{
namespace
{
double nonnegativePercent(double value) { return std::max(0.0, value / 100); }

double multiplyPercents(const std::vector<int> & values)
{
	double result = 1.0;
	for(const auto value : values) result *= nonnegativePercent(value);
	return result;
}

double apply(double base, double factor, double cap, bool stabilize = false)
{
	double damage = base * factor;
	if(stabilize)
	{
		const double nearest = std::floor(damage + 0.5);
		const double tolerance = std::max(1e-12, std::abs(damage) * 1e-14);
		if(std::abs(damage - nearest) <= tolerance) damage = nearest;
	}
	return std::min(cap, std::max(1.0, std::floor(damage)));
}

double casualties(const UnitFacts & defender, double damage)
{
	if(defender.phantomIntegrity > 0)
		return damage >= defender.phantomIntegrity ? std::max(0.0, defender.count) : 0;
	if(damage < defender.firstHealth) return 0;
	return std::min(1 + std::floor((damage - defender.firstHealth) / defender.maxHealth), defender.count);
}
}

DamageEstimation calculate(const DamageAttackInfo & info, const Facts & facts)
{
	const bool usesStage = hasPhysicalDamageReductionStage(info);
	const bool penetratesLegacy = info.physicalDamage && !usesStage && info.coordinatedPhysicalProtectionIgnorePercent > 0;
	double legacyMultiplier = 1.0;
	const auto [baseMin, baseMax] = baseDamage(info, facts);
	double raising = 1.0;
	double lowering = 1.0;
	const double heroOrder = info.heroOrderFinalDamageMultipliers.empty()
		? nonnegativePercent(info.heroOrderFinalDamageMultiplier) : multiplyPercents(info.heroOrderFinalDamageMultipliers);
	const double preemptive = nonnegativePercent(info.preemptiveDamageMultiplier);
	const double cleave = nonnegativePercent(info.cleaveFinalDamageMultiplier);
	const double archery = nonnegativePercent(info.archeryRangedDamageMultiplierPercent);
	const double followUp = nonnegativePercent(info.rangedFollowUpDamagePercent);
	const double activation = nonnegativePercent(info.activationOutputPercent);
	const double sylvan = multiplyPercents(info.sylvanAttackMultipliers);
	const double sylvanDefense = multiplyPercents(info.sylvanDefenseMultipliers)
		* nonnegativePercent(info.commandBeneficiaryDamageMultiplierPercent);
	const double specialty = static_cast<double>(info.ordinaryCreatureSpecialtyDamagePercent) / 100;
	double finalLuck = 1.0;
	if(info.newHorizonsFinalLuck && info.luckyStrike)
		finalLuck = 2.0 + facts.attacker.value(BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE) / 100;
	if(info.luckyStrike && info.sylvanLuckyStrikeTotalPercent > 0)
		finalLuck = static_cast<double>(info.sylvanLuckyStrikeTotalPercent) / 100;
	const double phantom = facts.defender.phantomIntegrity > 0 ? (info.physicalDamage ? 0.25 : 2.0) : 1.0;
	const double immovable = nonnegativePercent(info.bulwarkImmovableFinalDamageMultiplier);
	const double bastion = nonnegativePercent(info.armorerBastionFinalDamageMultiplier);
	const double counterBattery = nonnegativePercent(info.counterBatteryFinalDamageMultiplier);
	const double protection = usesStage ? physicalDamageReductionFactor(info, facts.physicalProtection) : 1.0;
	const auto allFactors = factors(info, facts);
	for(size_t index = 0; index < allFactors.size(); ++index)
	{
		const double factor = allFactors[index];
		if(factor > 0) raising += factor;
		else if(factor < 0)
		{
			if(penetratesLegacy && index >= 8 && index <= 12)
				legacyMultiplier *= 1 + std::max(-1.0, factor);
			else lowering *= 1 + std::max(-1.0, factor);
		}
	}
	double cap = std::numeric_limits<double>::infinity();
	const double capPercent = facts.defender.value(BonusType::DAMAGE_RECEIVED_CAP);
	if(capPercent > 0) cap = std::floor(facts.defender.maxHealth * capPercent / 100);
	const double inspiration = (100.0 + info.arcaneInspirationDamageBonusPercent) / 100
		* (100.0 - info.spellshockOutputPenaltyPercent) / 100;
	double damageFactor = raising * lowering * protection * inspiration
		* heroOrder * preemptive * cleave * archery * followUp
		* activation * sylvan * sylvanDefense * phantom * immovable * bastion * counterBattery
		* (static_cast<double>(info.frozenShatterFinalDamageMultiplier) / 100) * specialty * finalLuck;
	damageFactor = applyLegacyPhysicalProtection(info, damageFactor, usesStage, penetratesLegacy, legacyMultiplier);
	const bool stabilize = usesStage && protection < 1;
	const double low = apply(baseMin, damageFactor, cap, stabilize);
	const double high = apply(baseMax, damageFactor, cap, stabilize);
	const double beforeDefense = raising * heroOrder * preemptive * cleave * followUp * activation
		* sylvan * specialty * finalLuck * inspiration;
	return DamageEstimation{
		{static_cast<int64_t>(low), static_cast<int64_t>(high)},
		{static_cast<int64_t>(casualties(facts.defender, low)), static_cast<int64_t>(casualties(facts.defender, high))},
		{static_cast<int64_t>(apply(baseMin, beforeDefense, cap)), static_cast<int64_t>(apply(baseMax, beforeDefense, cap))}
	};
}
}

DamageEstimation NativeDamageCalculator::calculate(const CBattleInfoCallback & battle, DamageAttackInfo & info) const
{
	return nativeDamage::calculate(info, nativeDamage::capture(battle, info));
}
