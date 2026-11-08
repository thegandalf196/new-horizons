/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "UnitEffect.h"
#include "../../../constants/Enumerations.h"

namespace spells::effects::native
{
class DLL_LINKAGE Heal : public UnitEffect
{
public:
	void apply(ServerCallback *, const Mechanics *, const Target &) const override;
	SpellEffectValue getHealthChange(const Mechanics *, const Target &) const override;
protected:
	bool isValidTarget(const Mechanics *, const battle::Unit *) const override;
	EHealLevel configuredLevel() const;
	EHealLevel level(const Mechanics *) const;
	EHealPower power(const Mechanics *) const;
	int64_t minimumFullUnits(const Mechanics *) const;
	int64_t amount(const Mechanics *, const battle::Unit *) const;
};
}
