/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Client.h"
#include "CServerHandler.h"
#include "GameInstance.h"
#include "PerfTrace.h"
#include "../lib/UnlockGuard.h"
#include "../lib/callback/CGameInterface.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/networkPacks/PacksForServer.h"

int CClient::sendRequest(const CPackForServer & request, PlayerColor player, bool waitTillRealize)
{
	PerfTrace::Activity requestActivity("request_call_enter", "request_call_return");
	if(observerMode)
	{
		logNetwork->trace("Dropped request \"%s\" - a replay is in progress", typeid(request).name());
		return 0;
	}

	ui32 requestID = requestCounter++;
	logNetwork->trace("Sending a request \"%s\". It'll have an ID=%d.", typeid(request).name(), requestID);

	const auto * battleAction = dynamic_cast<const MakeAction *>(&request);
	const bool tracked = battleAction
		&& initialAutofight->prepareRequest(gamestate, player, requestID, battleAction->battleID);
	bool enqueued = false;
	bool transportEntered = false;
	try
	{
		waitingRequest.pushBack(requestID);
		enqueued = true;
		request.requestID = requestID;
		request.player = player;
		PerfTrace::submitted(request);
		PerfTrace::emit("transport_submit_enter");
		if(tracked) initialAutofight->enteringTransport(requestID);
		transportEntered = true;
		GAME->server().sendGamePack(request);
		if(tracked) initialAutofight->returnedFromTransport(requestID);
		PerfTrace::emit("transport_submit_return");
	}
	catch(...)
	{
		// Only a failure BEFORE entering transport proves no authority saw it.
		// A throw inside transport is uncertain and must never regain input.
		if(tracked && !transportEntered)
		{
			if(enqueued) waitingRequest.tryRemovingElement(requestID);
			initialAutofight->requestNotSubmitted(requestID);
		}
		else if(tracked)
			initialAutofight->failedInTransport(requestID);
		throw;
	}
	if(vstd::contains(playerint, player))
		playerint[player]->requestSent(&request, requestID);

	if(waitTillRealize)
	{
		logGlobal->trace("We'll wait till request %d is answered.\n", requestID);
		auto gsUnlocker = vstd::makeUnlockSharedGuard(CGameState::mutex);
		waitingRequest.waitWhileContains(requestID);
	}

	return requestID;
}
