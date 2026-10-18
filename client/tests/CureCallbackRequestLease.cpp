/* License: GNU General Public License v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CureCallbackRequestLease.h"
#include "../Client.h"
#include "../CPlayerInterface.h"
#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../battle/BattleInterface.h"
#include "../battle/BattleActionsController.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/networkPacks/PacksForServer.h"

void CureCallbackRequestLease::requireClient() const
{
	if(!ENGINE || !ENGINE->amIGuiThread() || !GAME || GAME->server().client.get() != client)
		throw std::runtime_error("Cure integration lease lost actual GUI/client ownership");
}

CureCallbackRequestLease::CureCallbackRequestLease(
	const std::shared_ptr<BattleInterface> & battle, CClient & actualClient)
	: client(&actualClient), owner(battle)
{
	requireClient();
	if(!battle || !battle->curInt || !battle->actionsController)
		throw std::runtime_error("Cure integration requires real owner/player/controller");
	player = battle->curInt;
	original = battle->curInt->cb;
	capturedGeneration = battle->actionsController->getCastingSession();
	if(!original || original->waitTillRealize || original->unlockGsWhenWaiting)
		throw std::runtime_error("Cure integration requires ordinary asynchronous callback");
	recording = std::make_shared<CCallback>(client->gameStatePtr(), original->getPlayerID(), this);
	for(const auto & [id, callback] : original->getActiveBattles())
		recording->onBattleStarted(callback->getBattle());
	battle->curInt->cb = recording;
}

CureCallbackRequestLease::~CureCallbackRequestLease()
{
	restore();
}

void CureCallbackRequestLease::restore()
{
	if(const auto live = player.lock(); live && live->cb == recording)
		live->cb = original;
	recording.reset();
	original.reset();
}

int CureCallbackRequestLease::sendRequest(const CPackForServer & request, PlayerColor color, bool wait)
{
	requireClient();
	if(wait)
		throw std::runtime_error("Cure integration must not synchronously wait under GUI ownership");
	++requests;
	if(const auto * action = dynamic_cast<const MakeAction *>(&request);
		action && action->ba.actionType == EActionType::HERO_SPELL)
	{
		const auto battle = owner.lock();
		endedBeforeSubmission = endedBeforeSubmission && battle
			&& battle->actionsController->getCastingSession() > capturedGeneration
			&& !battle->actionsController->heroSpellcastingModeActive();
		spellRequests.push_back(action->ba);
	}
	return client->sendRequest(request, color, false);
}

std::optional<BattleAction> CureCallbackRequestLease::makeSurrenderRetreatDecision(
	PlayerColor color, const BattleID & battle, const BattleStateInfoForRetreat & state)
{
	requireClient();
	return client->makeSurrenderRetreatDecision(color, battle, state);
}
