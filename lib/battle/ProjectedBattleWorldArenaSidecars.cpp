/*
 * ProjectedBattleWorldArenaSidecars.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "UnitTransitionSidecars.h"

void battle::ProjectedBattleWorldArena::Data::expireProtect()
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & list = orders.at(side);
		const auto found = std::find_if(list.begin(), list.end(), [](const HeroOrderState & order)
		{
			return order.command == HeroCommand::PROTECT;
		});
		if(found == list.end() || found->protectBroken)
			continue;
		const auto * protector = find(found->primaryTargetUnitId, false);
		const auto * ward = find(found->secondaryTargetUnitId, false);
		if(protectOrderIsSeparated(protector ? protector->state.get() : nullptr, ward ? ward->state.get() : nullptr))
			found->protectBroken = true;
	}
}

const std::vector<HeroOrderState> & battle::ProjectedBattleWorldArena::orderStates(BattleSide side) const
{
	impl->requireUsable();
	return impl->orders.at(side);
}

const ReducedExtraActivationState & battle::ProjectedBattleWorldArena::reducedActivation(BattleSide side) const
{
	impl->requireUsable();
	return impl->reduced.at(side);
}

battle::ProjectedUnitRemoval::Result battle::ProjectedBattleWorldArena::removeUnit(uint32_t id)
{
	impl->requireUsable();
	try
	{
		const auto result = removeValuesAndEdges(id);
		if(!result.completed)
			return result; // legacy missing-root/clone skips BOTH completion stages
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			clearReducedActivationForUnit(impl->reduced.at(side), id);
		impl->expireProtect();
		return result;
	}
	catch(...)
	{
		impl->abandon();
		throw;
	}
}
