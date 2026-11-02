/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CServerHandler.h"
#include "Client.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "Discord.h"
#include "mainmenu/CMainMenu.h"
#include "replay/GameplayReplayer.h"
#include "../lib/UnlockGuard.h"

void CServerHandler::endGameplay()
{
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

		gameplayReplayer.reset();
	}

	// nothing renders this map's texts any more, and the next game must not see them
	gameplayTextOverlays.clear();

	client->endNetwork();
	client->finishGameplay();

	// Game is ending
	// Tell the network thread to reach a stable state
	sendClientDisconnecting();
	logNetwork->info("Closed connection.");

	if(ownedObservation) waitForOwnedReceiveRetirement();
	client->endGame();
	client.reset();

	if (GAME->mainmenu())
	{
		GAME->mainmenu()->enable();
		GAME->mainmenu()->playMusic();
		GAME->mainmenu()->makeActiveInterface();
	}

	if(ENGINE)
		ENGINE->discord().setStatus("", "", {0, 0});
}
