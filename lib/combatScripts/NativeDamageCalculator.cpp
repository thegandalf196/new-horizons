/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeDamageCalculator.h"
#include "../battle/CBattleInfoCallback.h"
#include "NativeDamageFacts.h"
#include "NativeDamageFactors.h"
#include "NativeHellfireBatteryDamage.h"
#include "../battle/DamageInteger.h"

#include <cmath>

namespace nativeDamage
{
namespace
{
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

DamageEstimation calculateAttack(const DamageAttackInfo & info, const Facts & facts,
	const CBattleInfoCallback * callback, int firePermille)
{
	const bool usesStage = hasPhysicalDamageReductionStage(info);
	const bool penetratesLegacy = info.physicalDamage && !usesStage && info.coordinatedPhysicalProtectionIgnorePercent > 0;
	const auto [baseMin, baseMax] = baseDamage(info, facts);
	const auto modifiers = foldDamageModifiers(info, facts, firePermille > 0);
	const auto final = finalDamageFactors(info, facts);
	const double protection = usesStage ? physicalDamageReductionFactor(info, facts.physicalProtection) : 1.0;
	double cap = std::numeric_limits<double>::infinity();
	const double capPercent = facts.defender.value(BonusType::DAMAGE_RECEIVED_CAP);
	if(capPercent > 0) cap = std::floor(facts.defender.maxHealth * capPercent / 100);
	double damageFactor = finalDamageFactor(final, modifiers.raising * modifiers.lowering * protection, true);
	damageFactor = applyLegacyPhysicalProtection(info, damageFactor, usesStage, penetratesLegacy, modifiers.legacyProtection);
	const bool stabilize = usesStage && protection < 1;
	const double beforeDefense = modifiers.raising * final.heroOrder * final.preemptive * final.cleave * final.followUp * final.activation
		* final.sylvan * final.specialty * final.luck * final.inspiration;
	if(callback && firePermille > 0)
		return hellfireBattery::calculate(*callback, info, facts, damageFactor, beforeDefense, firePermille,
			commonDamageFactor(final, modifiers));
	const double low = apply(baseMin, damageFactor, cap, stabilize);
	const double high = apply(baseMax, damageFactor, cap, stabilize);
	return DamageEstimation{
		{damageInteger::boundedNonnegative(low), damageInteger::boundedNonnegative(high)},
		{damageInteger::boundedNonnegative(casualties(facts.defender, low)), damageInteger::boundedNonnegative(casualties(facts.defender, high))},
		{damageInteger::boundedNonnegative(apply(baseMin, beforeDefense, cap)), damageInteger::boundedNonnegative(apply(baseMax, beforeDefense, cap))}
	};
}

DamageEstimation calculate(const DamageAttackInfo & info, const Facts & facts)
{
	return calculateAttack(info, facts, nullptr, 0);
}
}

DamageEstimation NativeDamageCalculator::calculate(const CBattleInfoCallback & battle, DamageAttackInfo & info) const
{
	const auto * attacker = battle.preparedUnitDamageFacts(*info.attacker, info.shooting);
	const auto * defender = battle.preparedUnitDamageFacts(*info.defender, info.shooting);
	return nativeDamage::calculateAttack(info, attacker && defender
		? nativeDamage::capturePrepared(battle, info, *attacker, *defender)
		: nativeDamage::capture(battle, info), &battle, nativeDamage::hellfireBattery::firePermille(battle, info));
}
