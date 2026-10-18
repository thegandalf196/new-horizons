/*
 * ClientPackPublication.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CVCMIServer.h"
#include "CGameHandler.h"
#include "../lib/battle/ProjectedBattleWorldArena.h"
#include "../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/serializer/GameConnection.h"
#include <shared_mutex>
#include <stdexcept>

namespace
{
// Caller holds the real state mutex through subsequent publication and apply.
// Retire the private graph and its preparation owner BEFORE any transport call.
void preflightUnitChanges(CGameState & world, const BattleUnitsChanged & packet)
{
	NativeBonusGraphPreparation preparation;
	auto * battle = world.getBattle(packet.battleID);
	if(!battle)
		throw std::runtime_error("BattleUnitsChanged references a missing battle");
	battle::ProjectedBattleWorldArena projected(*battle);
	projected.applyPacket(packet);
}
}

void CVCMIServer::applyPack(CPackForClient & pack)
{
	// Refuse same-thread prepared reentry BEFORE sending. This short check
	// is not a publication lease; release graph access BEFORE transport.
	{ NativeBonusGraphMutation publicationAdmission; }
	const auto * changes = dynamic_cast<const BattleUnitsChanged *>(&pack);
	if(changes)
		NativeBonusGraphPreparation::checkEntry(); // refuse nested ownership before state relock
	logNetwork->trace("\tSending to all clients: %s", typeid(pack).name());
	{
		// One state interval covers ordered preflight, writers and actual apply.
		// Only the private graph interval ends before transport/serializer locks.
		std::unique_lock state(CGameState::mutex, std::defer_lock);
		if(changes)
		{
			state.lock();
			preflightUnitChanges(*gh->gs, *changes);
		}
		for(const auto & c : activeConnections)
			c->sendPack(pack);
		gh->gs->apply(pack);
	}
	logNetwork->trace("\tApplied on gameState(): %s", typeid(pack).name());
}
