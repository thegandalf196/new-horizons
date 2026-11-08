/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SpellEffect.h"
#include "../../../constants/Enumerations.h"
namespace spells::effects::native
{
class Catapult : public SpellEffect
{
public:
	bool applicableGeneral(Problem &, const Mechanics *) const override;
	void apply(ServerCallback *, const Mechanics *, const Target &) const override;
private:
	double hitChance(EWallPart part) const;
	int randomDamage(ServerCallback &) const;
	std::vector<EWallPart> potentialTargets(const CBattleInfoCallback &, bool bypassGate, bool bypassTower) const;
};
}
