/*
 * ProjectedUnitPacketWire.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <stdexcept>

void battle::ProjectedBattleWorldArena::applyPacket(const BattleUnitsChanged & packet)
{
	impl->requireUsable();
	try
	{
		if(packet.battleID != impl->battleId)
			throw std::invalid_argument("Projected packet requires its bound battle identity");
		// Match the OUTER GameState visitor: capture before the inner shape gates.
		const auto candidates = impl->captureCandidates(packet.changedStacks);
		packet.validateRebirthChainShape();
		packet.validatePhoenixSparkShape();
		if(!impl->applyConsumption(*this, packet))
			impl->applyEntries(*this, packet.changedStacks);
		// Inner consumption early returns do NOT skip the outer post-sidecars.
		impl->finishCandidates(*this, candidates, packet.changedStacks);
	}
	catch(...)
	{
		impl->abandon();
		throw;
	}
}

bool battle::ProjectedBattleWorldArena::rebirthChainUsed(BattleSide side) const
{
	impl->requireUsable();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid Rebirth Chain side");
	return impl->chainUsed.at(side);
}

bool battle::ProjectedBattleWorldArena::phoenixSparkUsed(BattleSide side) const
{
	impl->requireUsable();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid Phoenix Spark side");
	return impl->sparkUsed.at(side);
}
