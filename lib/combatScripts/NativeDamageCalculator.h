/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "IDamageCalculatorScript.h"

/// Fixed New Horizons damage mechanics. Saved policies are explicit payload
/// fields; no script lookup, virtual factor chain, or result memo is involved.
class DLL_LINKAGE NativeDamageCalculator final : public IDamageCalculatorScript
{
public:
	DamageEstimation calculate(const CBattleInfoCallback &, DamageAttackInfo &) const override;
	DamageForecastImplementation forecastImplementation(const CBattleInfoCallback &) const override
	{
		return DamageForecastImplementation::CANONICAL;
	}
};
