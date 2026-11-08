/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../Effect.h"
#include "../../../battle/Destination.h"

namespace spells::effects::native
{
/// Neutral base for location/structure effects whose targets are not units.
class DLL_LINKAGE SpellEffect : public Effect
{
public:
	void adjustAffectedHexes(BattleHexArray &, const Mechanics *, const Target &) const override {}
	void apply(ServerCallback *, const Mechanics *, const Target &) const override {}
	Target filterTarget(const Mechanics *, const Target & target) const override { return target; }
	Target transformTarget(const Mechanics *, const Target &, const Target & target) const override { return target; }
protected:
	JsonNode parameters;
	void initImpl(JsonNode data) override { parameters = std::move(data); }
};
}
