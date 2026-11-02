/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CServerHandler.h"
#include "Client.h"
#include "GameEngine.h"
#include "replay/GameplayReplayer.h"
#include "netlag/NetworkLagCompensator.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/bonuses/NativeBonusGraphAccess.h"
#include "../lib/bonuses/NativeBonusGraphAdmission.h"
#include "../lib/UnlockGuard.h"

void CServerHandler::visitForClient(CPackForClient & clientPack, ClientPacketReceiveSession * receive)
{
	if(gameplayReplayer && gameplayReplayer->isActive())
	{
		// a replay has taken the client over, so the live gamestate is not there to receive this pack.
		// Holding the network thread here keeps packs in order and applies them to the live session
		// once the replay is over - the player can end a replay at any time from its overlay
		auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
		gameplayReplayer->waitForFinish();
	}

	// not done in CGameState::apply() - lag compensation applies predictions and rollbacks there
	if(client)
	{
		NativeBonusGraphMutation nativeMutation;
		client->gameState().replayLog.recordPack(clientPack, client->gameState());
	}

	if (networkLagCompensator && networkLagCompensator->verifyReply(clientPack))
		return;

	if(receive) client->handleReceivedPack(clientPack, *receive);
	else client->handlePack(clientPack);
}
