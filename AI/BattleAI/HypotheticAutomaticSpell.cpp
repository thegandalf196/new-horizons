/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
void HypotheticBattle::setPassiveDivineMandateUses(BattleSide side, uint8_t uses)
{
	if(uses > 5 || uses < passiveDivineMandateUses.at(side))
		throw std::logic_error("Invalid passive Divine Mandate projection usage");
	passiveDivineMandateUses.at(side) = uses;
}
bool HypotheticBattle::projectAutomaticCounterspell(const ProjectedCounterspellOutcome & resolved)
{
	if(!resolved.resolutionKnown || !resolved.negated) return false;
	if(!resolved.wardActive) return true;
	const auto side = resolved.wardSide;
	if(!resolved.manaCost || !counterspellArmedStates.at(side)) return false;
	if(*resolved.negated)
	{
		const auto pools = getProjectedMana(side);
		if(!pools || pools->getTotal() < *resolved.manaCost) return false;
		auto next = ProjectedMana{*pools, getTemporaryBufferRemaining(side)};
		const int beforeBuffer = next.pools.getBuffer();
		if(!next.pools.spend(*resolved.manaCost)) return false;
		next.temporaryBufferRemaining = std::max(0, next.temporaryBufferRemaining
			- std::min(next.temporaryBufferRemaining, beforeBuffer - next.pools.getBuffer()));
		projectedMana.at(side) = std::move(next);
	}
	counterspellArmedStates.at(side) = false;
	countersequenceArmedStates.at(side) = false;
	return true;
}
