/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
void BattleActionProcessor::finishMeleeMovement(const CBattleInfoCallback & battle, const BattleAction & ba,
	int beforeAttackSpeed, int movementSpent, bool destroyedEnemy, bool vanishKill,
	bool allowPursuitContinuation, dreadCavalryAction::Context & dreadCavalry)
{
	const auto * resolvedAttacker = battle.battleGetStackByID(ba.stackNumber, false);
	const auto * ownerHero = resolvedAttacker ? battle.battleGetOwnerHero(resolvedAttacker) : nullptr;
	const int remainingMovement = std::max(0, beforeAttackSpeed - movementSpent);
	const int pursuitAllowance = destroyedEnemy && ownerHero
		&& ownerHero->hasActivePerk("new-horizons:offense", "new-horizons:offense.pursuit")
		? remainingMovement : 0;
	const int vanishAllowance = vanishKill && resolvedAttacker && !newHorizonsFrozen::isFrozen(*resolvedAttacker)
		&& !resolvedAttacker->armorerLastStandEndedActivation
		? newHorizonsShroud::vanishMovementAllowance(resolvedAttacker->getMovementRange(0)) : 0;
	const int opportunistAllowance = allowPursuitContinuation && resolvedAttacker
		? newHorizonsOpportunist::movementAllowance(battle, resolvedAttacker, remainingMovement) : 0;
	const int movementAllowance = std::max({pursuitAllowance, vanishAllowance, opportunistAllowance});
	dreadCavalry.movementSpent = movementSpent;
	dreadCavalry.newAllowance = movementAllowance;
	dreadCavalry.sharedTailCap = pursuitAllowance > 0 ? remainingMovement : opportunistAllowance > 0 ? 2 : 0;
	dreadCavalry.vanish = vanishAllowance;
	if(resolvedAttacker && resolvedAttacker->luckyOwnAttackSequence)
		finalizeOpportunistSequence(battle, resolvedAttacker, remainingMovement, allowPursuitContinuation);
	if(allowPursuitContinuation && movementAllowance > 0
		&& resolvedAttacker && resolvedAttacker->alive()
		&& resolvedAttacker->canMove() && !resolvedAttacker->isTimeStopped()
		&& !resolvedAttacker->armorerLastStandEndedActivation
		&& !newHorizonsFrozen::isFrozen(*resolvedAttacker))
	{
		setPursuitMovementRemaining(battle, resolvedAttacker, movementAllowance);

		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendRawString(opportunistAllowance > 0 ? "%s trigger post-attack movement and may move up to " : vanishAllowance > 0
			? (pursuitAllowance > 0 ? "%s trigger Pursuit and Vanish and may move up to "
				: "%s trigger Vanish and may move up to ")
			: "%s trigger Pursuit and may move up to ");
		resolvedAttacker->addNameReplacement(line, resolvedAttacker->getCount());
		line.appendNumber(movementAllowance);
		line.appendRawString(movementAllowance == 1 ? " hex." : " hexes.");
		message.lines.push_back(std::move(line));
		gameHandler->sendAndApply(message);
	}

}
