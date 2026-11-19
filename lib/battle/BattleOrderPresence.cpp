/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleInfo.h"

std::optional<bool> BattleInfo::heroOrderDamageStatePresence() const
{
	if(typeid(*this) != typeid(BattleInfo))
		return {};
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & state = sides.at(side);
		if(!state.orderStates.empty() || state.activeOrder != HeroCommand::NONE || state.focusFire)
			return true;
	}
	return false;
}

std::vector<HeroOrderState> BattleInfo::getHeroOrderStates(BattleSide side) const
{
	return sides.at(side).orderStates;
}
