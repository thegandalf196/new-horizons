/*
 * NewHorizonsDamagePower.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "NewHorizonsStormOfDaggersDamage.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/BattleInfo.h"
#include <limits>


namespace spells
{
namespace
{
int64_t checkedStormMultiply(int64_t left, int64_t right)
{
	if(left < 0 || right < 0
		|| (right != 0 && left > std::numeric_limits<int64_t>::max() / right))
		throw std::overflow_error("Storm of Daggers damage pool overflows");
	return left * right;
}

}

int64_t stormOfDaggersRoundedDamage(int64_t base, int64_t powerCoefficient, int32_t effectPower,
	int32_t powerDivisor, int32_t spellPowerCoefficientBasisPoints, int32_t warcastingBonusPercent,
	int32_t empowerSpellBonusPercent, int32_t selectedTargetCount, bool total)
{
	if(base < 0 || powerCoefficient < 0 || effectPower < 0 || powerDivisor <= 0
		|| spellPowerCoefficientBasisPoints < 0 || spellPowerCoefficientBasisPoints > 100000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000
		|| selectedTargetCount < 1
		|| selectedTargetCount > newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS)
		return 0;

	// Keep the authored fixed base and the full rational Spell Power component
	// together until the final displayed-total or equal-target rounding. The
	// generic direct-damage path truncates at the pool boundary, which differs
	// from the Storm table at positive half values (for example, SP=1 yields 47).
	const int64_t powerDenominator = checkedStormMultiply(
		checkedStormMultiply(powerDivisor, 1000000), 100);
	int64_t spellPowerNumerator = checkedStormMultiply(powerCoefficient, effectPower);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator, spellPowerCoefficientBasisPoints);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator,
		100LL + warcastingBonusPercent);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator,
		100LL + empowerSpellBonusPercent);
	const int64_t fixedNumerator = checkedStormMultiply(base, powerDenominator);
	if(spellPowerNumerator > std::numeric_limits<int64_t>::max() - fixedNumerator)
		throw std::overflow_error("Storm of Daggers damage pool overflows");
	const int64_t poolNumerator = fixedNumerator + spellPowerNumerator;

	const int64_t multiplierPercent = 100
		+ newHorizonsMagic::STORM_OF_DAGGERS_EXTRA_TARGET_DAMAGE_PERCENT * (selectedTargetCount - 1);
	const int64_t finalDivisor = (total ? 100LL : 100LL * selectedTargetCount);
	const int64_t denominator = checkedStormMultiply(powerDenominator, finalDivisor);
	const int64_t scaledNumerator = checkedStormMultiply(poolNumerator, multiplierPercent);
	if(scaledNumerator > std::numeric_limits<int64_t>::max() - denominator / 2)
		throw std::overflow_error("Storm of Daggers damage pool overflows");

	// The authored reference values use ordinary positive half-up rounding for
	// both the displayed total and the equal per-stack share, without first
	// truncating a fractional Spell Power term at the pool boundary.
	return (scaledNumerator + denominator / 2) / denominator;
}

int32_t Mechanics::getSpellPowerCoefficientBasisPoints() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return 10000;

	return newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(
		battleState->getMagicRules(), getHeroCaster(), getSpellId(), getCastSpellPowerComponentBonusPercent());
}

int64_t BaseMechanics::getStormOfDaggersDamagePerTarget(int32_t selectedTargetCount) const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	if(!isNewHorizonsStormOfDaggers() || !battleState)
		return 0;

	const auto formula = newHorizonsMagic::spellDirectDamage(
		battleState->getMagicRules(), owner->getJsonKey());
	if(!formula)
		return 0;
	return stormOfDaggersRoundedDamage(formula->base, formula->powerCoefficient,
		getEffectPower(), getEffectPowerDivisor(), getDirectDamageSpellPowerCoefficientBasisPoints(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), selectedTargetCount, false);
}

int64_t BaseMechanics::getStormOfDaggersTotalDamage(int32_t selectedTargetCount) const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	if(!isNewHorizonsStormOfDaggers() || !battleState)
		return 0;

	const auto formula = newHorizonsMagic::spellDirectDamage(
		battleState->getMagicRules(), owner->getJsonKey());
	if(!formula)
		return 0;
	return stormOfDaggersRoundedDamage(formula->base, formula->powerCoefficient,
		getEffectPower(), getEffectPowerDivisor(), getDirectDamageSpellPowerCoefficientBasisPoints(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), selectedTargetCount, true);
}


int32_t BaseMechanics::spellcraftCoefficient(newHorizonsHeroes::SpellcraftComponent component) const
{
	if(spellcraftContext.factors)
		return newHorizonsHeroes::composeSpellcraftCoefficient(*spellcraftContext.factors, component,
			getCastSpellPowerComponentBonusPercent());
	return Mechanics::getSpellPowerCoefficientBasisPoints();
}

int32_t BaseMechanics::getSpellPowerCoefficientBasisPoints() const
{
	return spellcraftCoefficient(newHorizonsHeroes::SpellcraftComponent::NUMERICAL);
}

int32_t BaseMechanics::getDamageSpellPowerCoefficientBasisPoints() const
{
	return spellcraftCoefficient(newHorizonsHeroes::SpellcraftComponent::DAMAGE);
}

int32_t BaseMechanics::getDirectDamageSpellPowerCoefficientBasisPoints() const
{
	return spellcraftCoefficient(newHorizonsHeroes::SpellcraftComponent::DIRECT_DAMAGE);
}
}
