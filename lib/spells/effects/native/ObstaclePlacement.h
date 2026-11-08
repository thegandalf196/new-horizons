/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "StructureCommon.h"

namespace spells::effects::native
{
class ObstaclePlacement final : public SpellEffect
{
public:
	bool applicableGeneral(Problem &, const Mechanics *) const override;
	bool applicableTarget(Problem &, const Mechanics *, const Target &) const override;
	Target transformTarget(const Mechanics *, const Target &, const Target &) const override;
	void adjustAffectedHexes(BattleHexArray &, const Mechanics *, const Target &) const override;
	void apply(ServerCallback *, const Mechanics *, const Target &) const override;
private:
	const JsonNode & options(BattleSide) const;
	static const JsonVector & paths(const JsonNode &);
	static BattleHex shape(BattleHex, const JsonNode &);
	static bool landMine(const Mechanics &);
	static bool fireWall(const Mechanics &);
	static bool quicksand(const Mechanics &);
	static bool selectedQuicksand(const Mechanics &);
	static bool selectedValid(const Mechanics &, const Target &, int count);
	SpellCreatedObstacle descriptor(const Mechanics &, BattleHex, const BattleHexArray &) const;
};
}
