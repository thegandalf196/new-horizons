/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleProcessor.h"
#include "BattleResultProcessor.h"

#include "../CGameHandler.h"

#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

void BattleProcessor::returnControlAfterRejectedAction(const BattleID & battleID, int32_t activeStackID,
	const BattleAction & ba)
{
	// BattleInterface relinquishes its local active stack before sending the
	// request. Return control explicitly, without advancing the queue or
	// replaying any activation lifecycle on server or clients.
	// A unit action for a stack that is not active answers an earlier prompt;
	// the active stack already received its own BattleSetActiveStack. Prompting
	// it again would queue a duplicate prompt, and the controller would answer
	// one prompt late, with every answer rejected, until the battle ends.
	if(activeStackID < 0 || (ba.isUnitAction() && ba.stackNumber != static_cast<uint32_t>(activeStackID)))
		return;
	const auto * currentBattle = gameHandler->gameState().getBattle(battleID);
	const auto * recoveryStack = currentBattle
		? currentBattle->battleGetStackByID(static_cast<uint32_t>(activeStackID), false) : nullptr;
	if(!currentBattle || resultProcessor->battleIsEnding(*currentBattle)
		|| !recoveryStack || !recoveryStack->alive() || currentBattle->getActiveStackID() != activeStackID)
		return;
	BattleSetActiveStack recovery;
	recovery.battleID = battleID;
	recovery.stack = static_cast<uint32_t>(activeStackID);
	recovery.reason = BattleUnitTurnReason::ACTION_REJECTED;
	gameHandler->sendAndApply(recovery);
}
