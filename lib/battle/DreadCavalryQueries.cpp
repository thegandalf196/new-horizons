/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsDreadCavalry.h"
#include "BattleInfo.h"
#include "PossiblePlayerBattleAction.h"
#include "../CStack.h"
bool CBattleInfoCallback::battleHasPendingDreadCavalry(const battle::Unit * unit) const
{
	return newHorizonsDreadCavalry::hasPending(*this, unit);
}
int CBattleInfoCallback::battleGetDreadCavalryDamagePercent(const battle::Unit * unit) const
{
	return newHorizonsDreadCavalry::outputPercent(*this, unit);
}
int CBattleInfoCallback::battleGetDreadCavalryMovementRemaining(const battle::Unit * unit) const
{
	return newHorizonsDreadCavalry::movementRemaining(*this, unit);
}

void BattleInfo::validateDreadCavalryStateSerialization(bool supported) const
{
	for(const auto & unit : stacks)
	{
		if(!unit)
			continue;
		const auto & state = unit->dreadCavalryState;
		state.validateShape();
		if(state.hasState() && !supported)
			throw std::runtime_error("Cannot discard Dread Cavalry battle state");
		if(!state.pending)
			continue;
		const bool suspended = std::ranges::any_of(crisisCommand.returns, [&](const auto & frame)
		{
			return frame.originalActor >= 0 && static_cast<uint32_t>(frame.originalActor) == unit->unitId();
		});
		if((getActiveStackID() < 0 || static_cast<uint32_t>(getActiveStackID()) != unit->unitId()) && !suspended)
			throw std::runtime_error("Dread Cavalry pending owner is neither active nor Crisis-suspended");
		if(!battleGetStackByID(state.victimId, false) || state.victimId == unit->unitId())
			throw std::runtime_error("Dread Cavalry victim identity is missing or self");
	}
}

bool CBattleInfoCallback::appendCreatureContinuationActions(const CStack * stack,
	std::vector<PossiblePlayerBattleAction> & actions) const
{
	if(stack->dreadCavalryState.pending)
	{
		if(battleHasPendingDreadCavalry(stack))
		{
			actions.push_back(PossiblePlayerBattleAction::ATTACK);
			actions.push_back(PossiblePlayerBattleAction::WALK_AND_ATTACK);
		}
		return true;
	}
	if(stack->pursuitMovementRemaining <= 0)
		return false;
	if(stack->canMove())
		actions.push_back(PossiblePlayerBattleAction::MOVE_STACK);
	return true;
}

void BattleInfo::retireRelinquishedDreadCavalry(uint32_t nextActor, BattleUnitTurnReason reason)
{
	if(activeStack < 0 || static_cast<uint32_t>(activeStack) == nextActor
		|| reason == BattleUnitTurnReason::CRISIS_ORDER)
		return;
	auto * previous = getStack(activeStack, false);
	if(!previous || !previous->dreadCavalryState.pending)
		return;
	if(!newHorizonsDreadCavalry::abandonsPendingOnActorHandoff(*this, nextActor, reason))
		return;
	// Ordinary Second Wind changes the actor without a return frame. The old
	// melee actor has already spent its action; this abandons only its charge.
	previous->dreadCavalryState.retirePending();
}
