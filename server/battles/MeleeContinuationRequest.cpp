/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/BattleAction.h"
std::optional<bool> BattleActionProcessor::handleMeleeContinuationRequest(const CBattleInfoCallback & battle,
	const BattleAction & ba, const battle::Unit * active, BattleSide controllingSide,
	BattleAction * effectiveActionOut, bool * masterGateActivationContinuationOut)
{
	const auto * activeStack = battle.battleGetStackByID(active->unitId(), false);
	if(activeStack && activeStack->dreadCavalryState.pending)
	{
		if(ba.actionType == EActionType::DEFEND || ba.actionType == EActionType::NO_ACTION)
		{
			BattleAction pass = BattleAction::makeNoAction(active);
			pass.side = controllingSide;
			if(effectiveActionOut)
				*effectiveActionOut = pass;
			return makeBattleActionImpl(battle, pass, masterGateActivationContinuationOut);
		}
		if(ba.isUnitAction() && ba.actionType != EActionType::WALK_AND_ATTACK)
			return false;
	}
	else if(activeStack && activeStack->pursuitMovementRemaining > 0)
	{
		if(ba.actionType == EActionType::DEFEND || ba.actionType == EActionType::NO_ACTION)
		{
			// Defend is the existing visible close-turn control. During Pursuit it
			// declines the optional movement without granting a defensive stance.
			BattleAction pass = BattleAction::makeNoAction(active);
			pass.side = controllingSide;
			if(effectiveActionOut)
				*effectiveActionOut = pass;
			return makeBattleActionImpl(battle, pass, masterGateActivationContinuationOut);
		}
		if(ba.isUnitAction() && ba.actionType != EActionType::WALK)
		{
			gameHandler->complain("Post-attack continuation permits only movement or ending the creature activation");
			return false;
		}
	}
	return std::nullopt;
}
