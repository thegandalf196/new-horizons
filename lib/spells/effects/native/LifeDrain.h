/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "Damage.h"

namespace spells::effects::native
{
class DLL_LINKAGE LifeDrain final : public Damage
{
public:
	ResistanceShareKind resistanceShareKind() const override { return ResistanceShareKind::LifeDrain; }
	void adjustTargetTypes(std::vector<TargetType> &, const Mechanics *) const override;
	bool applicableGeneral(Problem &, const Mechanics *) const override;
	bool applicableTarget(Problem &, const Mechanics *, const Target &) const override;
	Target transformTarget(const Mechanics *, const Target &, const Target &) const override;
	SpellEffectValue getHealthChange(const Mechanics *, const Target &) const override;
	void apply(ServerCallback *, const Mechanics *, const Target &) const override;
	ForecastImplementation forecastImplementation(const Mechanics *) const override { return ForecastImplementation::CANONICAL_LIFE_DRAIN; }
private:
	bool enemy(const Mechanics *, const battle::Unit *) const;
	bool friendly(const Mechanics *, const battle::Unit *) const;
	bool valid(const Mechanics *, const Target &) const;
};
}
