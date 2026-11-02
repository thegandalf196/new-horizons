/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Client.h"
#include "ClientNetPackVisitors.h"
#include "ClientBattleInterfaceDispatch.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "GameEngine.h"
#include "replay/ClientPacketReceiveSession.h"
#include "../lib/callback/CBattleGameInterface.h"
#include "../lib/UnlockGuard.h"
#include "../lib/battle/OwnedBattleFrameBatch.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/gameState/CGameState.h"
#include <utility>
#include <stdexcept>

void CClient::startPlayerBattleAction(const BattleID & battleID, PlayerColor color, ClientPacketReceiveSession * receive)
{
	if (battleints.count(color) == 0)
		return; // not our combat in MP

	auto battleint = battleints.at(color);
	std::shared_ptr<const battle::OwnedBattleFrameBatch> frames;
	if(battleint->usesOwnedRecipientPreparation())
	{
		if(battleint->human || !ENGINE || !receive)
		{ battleint->cancelOwnedPreparationObservation("Missing actual willing pre-unlock receive owner"); return; }
		try { frames = receive->captureActiveBattleFrames(color, battleID, battleint); }
		catch(const std::exception & error)
		{ battleint->cancelOwnedPreparationObservation(error.what()); return; }
		if(!frames) return; // Cancelled/refused owned decision never falls back to a native actor.
	}
	auto activateStack = [&]()
	{
		if(frames) battleint->activeStackWithOwnedFrames(battleID, nullptr, frames);
		else battleint->activeStack(battleID, gameState().getBattle(battleID)->battleGetStackByID(gameState().getBattle(battleID)->activeStack, false));
	};

	if (!battleint->human)
	{
		// we want to avoid locking gamestate and causing UI to freeze while AI is making turn
		if(ENGINE)
		{
			auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
			activateStack();
		}
		else
		{
			activateStack();
		}
	}
	else
	{
		activateStack();
	}
	if(frames)
	{
		try
		{
			if(!receive->observeAfterOwnedDecision(frames, battleint))
			{ battleint->cancelOwnedPreparationObservation("Actual client source retired before post-observation"); return; }
			battleint->completeOwnedPreparationObservation(); // No source/native lease survives this value-only completion.
		}
		catch(const std::exception & error) { battleint->cancelOwnedPreparationObservation(error.what()); }
	}

}

bool CClient::cancelOwnedRecipientPreparation(const std::string & reason)
{
	bool notified = false;
	for(const auto & [player, interface] : battleints)
		if(interface && interface->usesOwnedRecipientPreparation())
		{
			interface->cancelOwnedPreparationObservation(reason);
			notified = true;
		}
	return notified;
}

void ApplyClientNetPackVisitor::visitBattleSetActiveStack(BattleSetActiveStack & pack)
{
	callBattleInterfaceIfPresentForBothSides(cl, pack.battleID, &IBattleEventsReceiver::battleActiveStackReasonChanged,
		pack.battleID, pack.stack, pack.reason);

	if(pack.reason == BattleUnitTurnReason::AUTOMATIC_ACTION || pack.reason == BattleUnitTurnReason::CRISIS_RESUME)
		return;

	const auto playerToCall = [&]()
	{
		const auto * battle = gs.getBattle(pack.battleID);
		const CStack * activated = battle->battleGetStackByID(pack.stack);
		const auto & crisis = battle->getCrisisCommandState();
		return crisis.choice()
			? battle->sideToPlayer(crisis.chooser()) : battle->battleGetActionController(activated);
	}(); // Retire native unit/state borrows before preparation's observation wait.

	cl.startPlayerBattleAction(pack.battleID, playerToCall, receive);
}
