/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "AttackPossibility.h"

float AttackPossibility::damageDiff() const
{
	return defenderDamageReduce - attackerDamageReduce - collateralDamageReduce + shootersBlockedDmg + frozenControlValue + petrifyingBloodControlValue;
}

float AttackPossibility::damageDiff(float positiveEffectMultiplier, float negativeEffectMultiplier) const
{
	return positiveEffectMultiplier * (defenderDamageReduce + shootersBlockedDmg + std::max(0.0f, frozenControlValue) + std::max(0.0f, petrifyingBloodControlValue))
		- negativeEffectMultiplier * (attackerDamageReduce + collateralDamageReduce + std::max(0.0f, -frozenControlValue) + std::max(0.0f, -petrifyingBloodControlValue));
}

float AttackPossibility::attackValue() const
{
	return damageDiff();
}
