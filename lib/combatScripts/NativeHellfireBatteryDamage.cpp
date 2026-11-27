/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeHellfireBatteryDamage.h"
#include "NativeDamageFacts.h"
#include "NativeDamageFactors.h"
#include "IDamageCalculatorScript.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/DamageInteger.h"
#include "../battle/NewHorizonsMagicalAbilityDamage.h"
#include "../battle/NewHorizonsPassiveSpecialties.h"
#include "../battle/Unit.h"
#include "../entities/hero/NewHorizonsInfernoSpecialties.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace nativeDamage::hellfireBattery
{
namespace
{
int64_t casualties(const UnitFacts & target, int64_t damage)
{
	if(target.phantomIntegrity > 0)
		return damage >= target.phantomIntegrity ? damageInteger::boundedNonnegative(target.count) : 0;
	if(damage < target.firstHealth)
		return 0;
	return damageInteger::boundedNonnegative(std::min(target.count,
		1 + std::floor((damage - target.firstHealth) / target.maxHealth)));
}

int64_t boundDamage(const CBattleInfoCallback & battle, const battle::Unit & target,
	double commonDamage, double physicalDamage, int share, int64_t cap)
{
	const auto budget = splitBudget(commonDamage, share);
	const auto totalBudget = budget.physical + budget.fire;
	const double physicalFraction = totalBudget > 0
		? static_cast<double>(budget.physical) / static_cast<double>(totalBudget) : 0;
	const int64_t physical = budget.physical > 0
		? damageInteger::boundedNonnegative(std::floor(physicalDamage * physicalFraction)) : 0;
	const int64_t fire = newHorizonsPassiveSpecialties::magicalDamageImmune(target) ? 0
		: newHorizonsMagicalAbilityDamage::adjustFireDamage(battle, target, budget.fire);
	const int64_t combined = physical > std::numeric_limits<int64_t>::max() - fire
		? std::numeric_limits<int64_t>::max() : physical + fire;
	// The ordinary minimum belongs to the remaining physical hit, once. A fully
	// converted hit stopped by Fire immunity must remain zero.
	return std::min(cap, std::max<int64_t>(budget.physical > 0 ? 1 : 0, combined));
}
}

Budget splitBudget(double commonDamage, int share)
{
	if(share < 0 || share > 1000)
		throw std::invalid_argument("Hellfire Battery Fire share must be between zero and one thousand permille");
	const int64_t total = damageInteger::boundedNonnegative(std::max(1.0, std::floor(commonDamage)));
	const int64_t fire = total / 1000 * share + total % 1000 * share / 1000;
	return {total - fire, fire};
}

int firePermille(const CBattleInfoCallback & battle, const DamageAttackInfo & info)
{
	if(!info.physicalDamage || !info.shooting || !info.attacker || !info.attacker->isBallista() || !info.defender)
		return 0;
	return newHorizonsHeroes::hellfireBatteryFirePermille(battle.battleGetOwnerHero(info.attacker));
}

DamageEstimation calculate(const CBattleInfoCallback & battle, const DamageAttackInfo & info,
	const Facts & facts, double physicalFactor, double beforeDefenseFactor, int share, double common)
{
	const auto [baseMin, baseMax] = baseDamage(info, facts);
	const double capPercent = facts.defender.value(BonusType::DAMAGE_RECEIVED_CAP);
	const int64_t cap = capPercent > 0
		? damageInteger::boundedNonnegative(std::floor(facts.defender.maxHealth * capPercent / 100))
		: std::numeric_limits<int64_t>::max();
	const int64_t low = boundDamage(battle, *info.defender, baseMin * common, baseMin * physicalFactor, share, cap);
	const int64_t high = boundDamage(battle, *info.defender, baseMax * common, baseMax * physicalFactor, share, cap);
	const auto beforeDefense = [&](double base)
	{
		return std::min(cap, damageInteger::boundedNonnegative(std::max(1.0, std::floor(base * beforeDefenseFactor))));
	};
	return DamageEstimation{{low, high}, {casualties(facts.defender, low), casualties(facts.defender, high)},
		{beforeDefense(baseMin), beforeDefense(baseMax)}};
}
}
