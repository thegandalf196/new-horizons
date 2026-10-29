/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "DreadCavalryAction.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsDreadCavalry.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace
{
void publish(CGameHandler & handler, const CBattleInfoCallback & battle,
	const CStack * source, const std::shared_ptr<battle::CUnitState> & state)
{
	BattleUnitsChanged changed;
	changed.battleID = battle.getBattle()->getBattleID();
	UnitChanges update(source->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	changed.changedStacks.push_back(std::move(update));
	handler.sendAndApply(changed);
}
}
namespace dreadCavalryAction
{
Context begin(const CBattleInfoCallback & battle, const CStack * source, bool allowContinuation)
{
	Context result;
	if(!source)
		return result;
	result.actor = source->unitId();
	result.before = source->dreadCavalryState;
	result.controller = battle.playerToSide(battle.battleGetActionController(source));
	result.movementBudget = source->getMovementRange(0);
	if(result.before.pending)
	{
		result.enabled = battle.battleHasPendingDreadCavalry(source);
		result.percent = result.before.damagePercent;
		result.movementBudget = std::min(result.movementBudget, result.before.remainingMovement);
	}
	else if(newHorizonsDreadCavalry::entitlement(battle, source))
		result.enabled = allowContinuation && !result.before.usedThisActivation;
	return result;
}
bool acceptedBlowStillLegal(const CBattleInfoCallback & battle, const CStack * source,
	const CStack * target, const Context & context)
{
	return (!context.before.pending || context.enabled)
		&& newHorizonsDreadCavalry::acceptedBlowStillLegal(battle, source, target, context.actor, context.before);
}
void observeKilled(const CBattleInfoCallback & battle, const CStack * source,
	const BattleAttack & resolved, Context & context)
{
	if(!context.enabled || context.before.pending || context.firstVictim != battle::DreadCavalryState::NO_VICTIM)
		return;
	for(const auto & hit : resolved.bsa)
	{
		const auto * victim = battle.battleGetUnitByID(hit.stackAttacked);
		if(victim && hit.killed() && !hit.willRebirth() && !victim->alive()
			&& battle.battleMatchOwner(source, victim) && battle.battleMatchActionController(source, victim))
		{
			context.firstVictim = victim->unitId();
			return;
		}
	}
}
void complete(CGameHandler & handler, const CBattleInfoCallback & battle, const Context & context)
{
	if(!context.before.pending && (!context.enabled || context.firstVictim == battle::DreadCavalryState::NO_VICTIM))
		return;
	const auto * source = battle.battleGetStackByID(context.actor, false);
	if(!source)
		return;
	auto state = source->acquireState();
	if(context.before.pending)
	{
		state->dreadCavalryState.retirePending();
		state->pursuitMovementRemaining = source->alive() && source->canMove() && !source->isTimeStopped()
			&& !newHorizonsFrozen::isFrozen(*source) && !source->armorerLastStandEndedActivation
			? std::max(context.before.legacyTailAfterMovement(context.movementSpent), context.newAllowance) : 0;
		publish(handler, battle, source, state);
		return;
	}
	const auto grant = newHorizonsDreadCavalry::grantAfterMelee(battle, source, context.firstVictim,
		context.controller, std::max(0, context.movementBudget - context.movementSpent), context.sharedTailCap, context.vanish);
	if(!grant)
		return;
	state->dreadCavalryState = *grant;
	publish(handler, battle, source, state);
	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();
	message.lines.push_back(MetaString::createFromRawString(
		"Dread Cavalry: choose a different enemy for a reduced-damage melee attack, or skip the charge."));
	handler.sendAndApply(message);
}
void decline(CGameHandler & handler, const CBattleInfoCallback & battle, const CStack * source)
{
	if(!source || !source->dreadCavalryState.pending)
		return;
	auto state = source->acquireState();
	state->dreadCavalryState.retirePending();
	publish(handler, battle, source, state); // Legacy Pursuit allowance is deliberately retained.
}
}

bool BattleActionProcessor::doMeleeActionWithContinuation(const CBattleInfoCallback & battle,
	const BattleAction & action, bool allowPursuitContinuation)
{
	auto context = dreadCavalryAction::begin(battle, battle.battleGetStackByID(action.stackNumber, false), allowPursuitContinuation);
	const bool accepted = doAttackAction(battle, action, allowPursuitContinuation, context);
	if(accepted)
		dreadCavalryAction::complete(*gameHandler, battle, context);
	return accepted;
}
