/*
 * PreparedClientReplayFactory.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "../CServerHandler.h"
#include "../Client.h"
#include "../netlag/NetworkLagCompensator.h"
#include "PreparedClientBattleReplay.h"
#include <stdexcept>

std::unique_ptr<PreparedClientBattleReplay> CServerHandler::prepareBattleReplayEntry(BattleUnitsChanged & packet)
{
	if(!client)
		throw std::logic_error("Client replay entry requires the active real client");
	const auto world = client->gameStatePtr();
	if(networkLagCompensator)
		return networkLagCompensator->prepareBattleReplayEntry(world, packet);
	return prepareClientBattleReplayEntry(world, packet, nullptr);
}
