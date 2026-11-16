/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Client.h"
#include "GameEngine.h"
#include "GameInstance.h"
#include "CServerHandler.h"
#include "CPlayerInterface.h"
#include "battle/BattleInterface.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/GameLibrary.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "../lib/UnlockGuard.h"
#include "../lib/callback/CBattleCallback.h"
#include "../lib/callback/CCallback.h"
#include "../lib/callback/CBattleGameInterface.h"

bool CClient::canStartInitialAutofight() const
{
	return !observerMode && initialAutofight->acceptsRequest();
}

bool CClient::startInitialAutofight(const std::shared_ptr<CBattleGameInterface> & controller,
	const std::shared_ptr<CBattleCallback> & callback, BattleID battle, std::optional<uint32_t> unit)
{
	if(observerMode || !callback)
		return false;
	const auto player = callback->getPlayerID();
	if(!player || !battleCallbacks.contains(*player) || battleCallbacks.at(*player) != callback)
		return false;
	if(callback->waitTillRealize)
	{
		logGlobal->error("Initial auto-fight refused a blocking battle controller callback");
		return false;
	}
	const auto * currentBattle = gamestate->getBattle(battle);
	if(!currentBattle) return false;
	return initialAutofight->start({gamestate, callback, controller, battle, unit, currentBattle->getActivationSerial()});
}

bool CClient::drainInitialAutofight()
{
	// No CClient member access after unlocking until its identity is checked.
	auto task = initialAutofight;
	auto source = gamestate;
	auto pause = task->pauseAdmissions();
	if(task->idle()) return true; // No yield/reentry for the ordinary packet path.
	if(ENGINE)
	{
		{
			auto unlock = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			task->wait();
		}
	}
	else
		task->wait();
	if(!GAME)
		return false;
	auto * current = GAME->server().client.get();
	if(current != this || current->initialAutofight != task || current->gamestate != source)
		return false; // Check retained identities too: a new client can reuse an address.
	return true;
}

CClient::GameplaySource CClient::retainGameplaySource() const
{
	return {this, gamestate, initialAutofight};
}

bool CClient::matchesGameplaySource(const GameplaySource & source) const
{
	return this == source.client && initialAutofight == source.task && gamestate == source.world;
}

bool CClient::matchesRetiredGameplaySource(const GameplaySource & source) const
{
	return this == source.client && initialAutofight == source.task && !gamestate;
}

bool CClient::pollInitialAutofight()
{
	auto task = initialAutofight;
	if(!task->completed()) return true;
	[[maybe_unused]] const auto interfaces = playerint; // Retain any caller while drain unlocks the GUI.
	if(!drainInitialAutofight()) return false;
	const auto completion = task->peekCompletion();
	if(!completion) return true;
	if(completion->source.world != gamestate)
	{
		(void)task->takeCompletion();
		return true; // Superseded source cannot block the installed session.
	}
	const auto & source = completion->source;
	const auto player = source.callback->getPlayerID();
	const auto found = player ? playerint.find(*player) : playerint.end();
	const auto human = found != playerint.end()
		? std::dynamic_pointer_cast<CPlayerInterface>(found->second) : nullptr;
	if(!human || human->autofightingAI != source.controller || human->cb != source.callback
		|| !player || !battleCallbacks.contains(*player) || battleCallbacks.at(*player) != source.callback)
	{
		(void)task->takeCompletion();
		return true; // Replaced controllers/callbacks cannot hold new input hostage.
	}
	if(completion->result == InitialAutofightResult::FAILED)
		unregisterBattleInterface(source.controller, *player); // Stop tactics/actionFinished continuations now.
	if(completion->hasUncertainTransport())
	{
		if(!CPlayerInterface::battleInt || CPlayerInterface::battleInt->getBattleID() != source.battle)
		{
			(void)task->takeCompletion();
			return true;
		}
		// Unknown transport delivery cannot regain input or wait for a fictional
		// acknowledgement. End only this still-current session normally.
		if(GAME->server().endGameplay())
			GAME->server().showServerError(LIBRARY->generaltexth->translate("vcmi.server.errors.disconnected"));
		return false; // endGameplay may have deleted this CClient and its caller.
	}
	if(completion->result != InitialAutofightResult::COMPLETED && completion->waitingForRealization())
		return true; // Exact existing PackageApplied receipts will unblock recovery.
	if(!human->completeInitialAutofight(*completion)) return true;
	(void)task->takeCompletion();
	return true;
}

void CClient::initialAutofightRequestRealized(const PackageApplied & receipt)
{
	initialAutofight->requestRealized(gamestate, receipt.player, receipt.requestID, receipt.result);
}
