/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../Effect.h"

namespace spells::effects::native
{
/// Shared native targeting for the fixed unit spell effects. Each derived
/// effect supplies its validity/receptivity rules without a script dispatch.
class DLL_LINKAGE UnitEffect : public Effect
{
public:
	void adjustAffectedHexes(BattleHexArray &, const Mechanics *, const Target &) const override;
	bool applicableGeneral(Problem &, const Mechanics *) const override;
	bool applicableTarget(Problem &, const Mechanics *, const Target &) const override;
	void apply(ServerCallback *, const Mechanics *, const Target &) const override {}
	Target filterTarget(const Mechanics *, const Target &) const override;
	Target transformTarget(const Mechanics *, const Target &, const Target &) const override;

protected:
	JsonNode parameters;
	void initImpl(JsonNode data) override { parameters = std::move(data); }
	virtual bool isValidTarget(const Mechanics *, const battle::Unit *) const;
	virtual bool isReceptive(const Mechanics *, const battle::Unit *) const;
	Target transformByRange(const Mechanics *, const Target &, const Target &) const;
	Target transformByChain(const Mechanics *, const Target &, const Target &, int chainLength) const;
};
}
