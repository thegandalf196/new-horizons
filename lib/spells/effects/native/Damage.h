/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "UnitEffect.h"

namespace nativeEvents { struct Context; }
namespace spells::effects::native
{
class DLL_LINKAGE Damage : public UnitEffect
{
public:
	void apply(ServerCallback *, const Mechanics *, const Target &) const override;
	ResistanceShareKind resistanceShareKind() const override;
	SpellEffectValue getHealthChange(const Mechanics *, const Target &) const override;
	ForecastImplementation forecastImplementation(const Mechanics *) const override { return ForecastImplementation::CANONICAL_DAMAGE; }
	int64_t damageForTarget(size_t index, const Mechanics *, const battle::Unit *) const;
protected:
	bool isReceptive(const Mechanics *, const battle::Unit *) const override;
	std::pair<int64_t, int64_t> collateral(const nativeEvents::Context &, const Mechanics *, const battle::Unit *, int64_t) const;
	void describe(const nativeEvents::Context &, const Mechanics *, const battle::Unit *, int64_t kills,
		int64_t damage, bool multiple, int64_t ordinaryKills, int64_t ordinaryDamage, int64_t executedKills) const;
};
}
