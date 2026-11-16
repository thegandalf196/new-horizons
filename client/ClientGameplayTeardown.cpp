/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CServerHandler.h"
#include "Client.h"
#include "ClientGameplayRetirement.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "Discord.h"
#include "mainmenu/CMainMenu.h"
#include "replay/GameplayReplayer.h"
#include "../lib/UnlockGuard.h"
#include "../lib/serializer/GameConnection.h"

bool CServerHandler::endGameplay()
{
	if(!client) return false;
	const auto source = client->retainGameplaySource();
	const auto current = [&] { return client && client->matchesGameplaySource(source); };
	// Notify the actual battle-only controller before ANY replay/server wait.
	const bool ownedObservation = client && client->cancelOwnedRecipientPreparation("gameplay teardown before source retirement");
	// a running replay holds the live session hostage - it has to be gone before anything is torn down.
	// Every caller of this method owns the interface mutex, which the replay thread needs to finish.
	if(gameplayReplayer)
	{
		gameplayReplayer->requestStop();

		if(ENGINE)
		{
			auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			gameplayReplayer->waitForFinish();
		}
		else
		{
			gameplayReplayer->waitForFinish();
		}

		if(!current()) return false;
		gameplayReplayer.reset();
	}

	// nothing renders this map's texts any more, and the next game must not see them
	gameplayTextOverlays.clear();

	if(!current() || !retireClientGameplay(client, [&]
	{
		sendClientDisconnecting();
		logNetwork->info("Closed connection.");
		if(ownedObservation) waitForOwnedReceiveRetirement();
		return current();
	}))
		return false;

	if (GAME->mainmenu())
	{
		GAME->mainmenu()->enable();
		GAME->mainmenu()->playMusic();
		GAME->mainmenu()->makeActiveInterface();
	}

	if(ENGINE)
		ENGINE->discord().setStatus("", "", {0, 0});
	return true;
}

bool CServerHandler::restartGameplay()
{
	// Restart and quickload do not pass through the ordinary BattleEnded path.
	// Stop map/battle waiters before destroying interfaces, matching endGameplay
	// so no old combat callback can outlive the state it references.
	if(!retireClientGameplay(client, [] { return true; }))
		return false;
	// Teardown may still translate old map widgets. Drop their overlays only
	// after it finishes; startGameplay installs the new map and current campaign
	// scenario texts, including the sources needed by transferred heroes.
	gameplayTextOverlays.clear();

	logicConnection->enterLobbyConnectionMode();
	return true;
}
