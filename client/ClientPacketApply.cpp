/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Client.h"
#include "ClientNetPackVisitors.h"
#include "PerfTrace.h"
#include "../lib/gameState/CGameState.h"

void CClient::handlePack(CPackForClient & pack)
{
	applyClientPacket(pack, nullptr);
}
void CClient::handleReceivedPack(CPackForClient & pack, ClientPacketReceiveSession & receive)
{
	// No CClient access after apply: a PlayerEndsGame packet may delete it.
	applyClientPacket(pack, &receive);
}
void CClient::applyClientPacket(CPackForClient & pack, ClientPacketReceiveSession * receive)
{
	PerfTrace::ApplyScope trace(pack);
	ApplyClientNetPackVisitor afterVisitor(*this, gameState(), receive);
	ApplyFirstClientNetPackVisitor beforeVisitor(*this, gameState());

	pack.visit(beforeVisitor);
	logNetwork->trace("\tMade first apply on cl: %s", typeid(pack).name());
	{
		std::unique_lock lock(CGameState::mutex);
		gameState().apply(pack);
	}
	trace.stateApplied();
	logNetwork->trace("\tApplied on gs: %s", typeid(pack).name());
	pack.visit(afterVisitor);
	logNetwork->trace("\tMade second apply on cl: %s", typeid(pack).name());
}
