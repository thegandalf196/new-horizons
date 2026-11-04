/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandate.h"
#include "PassiveDivineMandateVanguard.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include <algorithm>

namespace newHorizonsPassiveDivineMandate
{
OrderRiders activeOrderRiders(const CBattleInfoCallback & battle, const battle::Unit * unit,
	const std::vector<HeroOrderState> & orders)
{
	if(!battle.getBattle() || !unit)
		return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	Policy active;
	for(const auto & order : orders)
	{
		if(order.command == HeroCommand::VANGUARD || !order.passiveDivineMandate
			|| !battle.battleOrderBenefitAppliesTo(order, side, unit))
			continue;
		// Vanguard uses its per-unit native entitlement below: the latest side
		// snapshot may belong to a different recipient while an older grant pauses.
		// Several live Orders never multiply the same perk. Each snapshot is an
		// issuing-time policy; no installed/current hero content is consulted.
		active.rank = std::max(active.rank, order.passiveDivineMandate->rank);
		active.perks |= order.passiveDivineMandate->perks;
	}
	const auto vanguardFlags = vanguardRiderPerks(unit, side);
	if(vanguardFlags != 0)
	{
		active.rank = std::max<uint8_t>(active.rank, 1); // Fixed riders do not use mastery magnitude.
		active.perks |= vanguardFlags;
	}
	return orderRiders(active);
}

OrderRiders activeOrderRiders(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	if(!battle.getBattle() || !unit)
		return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	return activeOrderRiders(battle, unit, battle.getBattle()->getHeroOrderStates(side));
}
}
