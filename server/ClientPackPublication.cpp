/*
 * ClientPackPublication.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CVCMIServer.h"
#include "CGameHandler.h"
#include "../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/serializer/GameConnection.h"
#include <shared_mutex>

void CVCMIServer::applyPack(CPackForClient & pack)
{
	// Refuse same-thread prepared reentry BEFORE sending. This short check
	// is not a publication lease; release graph access BEFORE transport.
	{ NativeBonusGraphMutation publicationAdmission; }
	logNetwork->trace("\tSending to all clients: %s", typeid(pack).name());
	for (const auto & c : activeConnections)
		c->sendPack(pack);
	{
		// State before graph (CGameState::apply), never graph before transport.
		std::unique_lock state(CGameState::mutex, std::defer_lock);
		if(dynamic_cast<BattleUnitsChanged *>(&pack))
			state.lock();
		gh->gs->apply(pack);
	}
	logNetwork->trace("\tApplied on gameState(): %s", typeid(pack).name());
}
