/*
 * ClientPacketReceiveSession.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "ClientPacketReceiveSession.h"
#include "PreparedClientBattleReplay.h"
#include "../CServerHandler.h"
#include "../Client.h"
#include "../GameEngine.h"
#include "../netlag/NetworkLagCompensator.h"
#include "GameplayReplayer.h"
#include <stdexcept>

ClientPacketReceiveSession::ClientPacketReceiveSession(CServerHandler & handler,
	std::unique_lock<std::mutex> && heldAccess, GameEngine * engine,
	const std::shared_ptr<INetworkConnection> & connection)
	: handler(handler), engine(engine), connection(connection), logic(handler.logicConnection),
	  client(handler.client.get()), predictions(handler.networkLagCompensator.get()),
	  world(client ? client->gameStatePtr().get() : nullptr),
	  access(std::move(heldAccess), engine ? &engine->interfaceMutex : nullptr)
{
}

ClientPacketReceiveSession::~ClientPacketReceiveSession() = default;

void ClientPacketReceiveSession::visitForClient(CPackForClient & packet)
{
	if(activePacket || access.child)
		throw std::logic_error("Receive dispatch is not reentrant");
	activePacket = &packet;
	try { handler.visitForClient(packet); } // EXACT existing dispatcher; no partial routing
	catch(...) { activePacket = nullptr; throw; }
	activePacket = nullptr;
}

bool ClientPacketReceiveSession::ready(const BattleUnitsChanged & packet) const noexcept
{
	// Short-circuit thread/lock before any live session lookup; no callback here.
	return access.heldOnThisThread() && ENGINE.get() == engine && activePacket == &packet
		&& handler.networkConnection == connection && handler.logicConnection == logic
		&& client && handler.client.get() == client && predictions
		&& handler.networkLagCompensator.get() == predictions
		&& client->gameStatePtr().get() == world
		&& handler.getState() != EClientState::DISCONNECTING
		&& handler.getState() != EClientState::CONNECTION_CANCELLED
		&& (!handler.gameplayReplayer || !handler.gameplayReplayer->isActive());
}

PreparedClientBattleReplay & ClientPacketReceiveSession::prepareConsumedReply(BattleUnitsChanged & packet)
{
	if(!ready(packet))
		throw std::logic_error("Consumed preparation requires the exact active native receive session");
	return access.prepareConsumed(client->gameStatePtr(), packet, *predictions, *this);
}
