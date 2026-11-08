/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleFlowProcessor.h"
#include "BattleActivationEffects.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#ifdef NH_OWNED_PREPARATION_SIMULATION_AUDIT
#include "../tests/OwnedPreparationAudit.h"
#endif

bool BattleFlowProcessor::setActiveStack(const CBattleInfoCallback & battle, const battle::Unit * stack, BattleUnitTurnReason reason)
{
#ifdef NH_OWNED_PREPARATION_SIMULATION_AUDIT
	const auto auditBattleID = battle.getBattle()->getBattleID();
	const auto auditUnitID = stack->unitId();
#endif
	assert(stack);
	const auto stunBattleID = battle.getBattle()->getBattleID();
	const auto stunUnitID = stack->unitId();
	const auto * stunBattleOwner = gameHandler->gameState().getBattle(stunBattleID);
	const bool stunnedActivation = newHorizonsStun::forfeitsActivation(battle, *stack, reason);
	if(newHorizonsFrozen::isFrozen(*stack) && reason != BattleUnitTurnReason::TURN_QUEUE
		&& reason != BattleUnitTurnReason::AUTOMATIC_ACTION)
	{
		// Earned extras/continuations are unusable, not normal queue forfeitures.
		// Do not remove Frozen or run any start-of-activation effects here.
		clearReducedActivation(battle, stack->unitId());
		const auto side = battle.playerToSide(battle.battleGetOwner(stack));
		if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		{
			const auto secondWind = battle.getBattle()->getHeroOrderState(side, HeroCommand::SECOND_WIND);
			if(secondWind && secondWind->secondWindActive
				&& secondWind->primaryTargetUnitId == stack->unitId())
			{
				if(const auto * state = dynamic_cast<const BattleInfo *>(battle.getBattle()))
				{
					if(const_cast<BattleInfo *>(state)->setHeroOrderSecondWindActive(side, false))
						publishHeroOrderState(battle, side);
				}
			}
		}
		activateNextStack(battle);
		return true;
	}

	BattleSetActiveStack sas;
	sas.battleID = battle.getBattle()->getBattleID();
	sas.stack = stack->unitId();
	sas.reason = reason;
	gameHandler->sendAndApply(sas);
	if(battle.battleBeginsActivation(stack, reason))
		battleActivationEffects::apply(gameHandler, battle, stack, reason);
	{
		const auto * current = gameHandler->gameState().getBattle(stunBattleID);
		if(!current || current != stunBattleOwner) return true;
		stack = current->battleGetUnitByID(stunUnitID);
		if(!stack)
		{
			if(stunnedActivation) completeStunnedActivation(*current, stunUnitID);
			return true;
		}
	}
	bool secondWindActivation = false;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		const auto controllerSide = battle.playerToSide(battle.battleGetOwner(stack));
		if(controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		{
			const auto state = battle.getBattle()->getHeroOrderState(controllerSide, HeroCommand::SECOND_WIND);
			secondWindActivation = state
				&& state->secondWindActive
				&& state->primaryTargetUnitId == stack->unitId();
		}
	}
	if(stack->alive() && !stack->isTimeStopped()
		&& (reason == BattleUnitTurnReason::TURN_QUEUE || reason == BattleUnitTurnReason::MORALE
			|| reason == BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION || secondWindActivation)
		&& activationFireWallCoversUnit(battle, *stack))
		battle.handleObstacleTriggersForUnit(*gameHandler->spellEnv, *stack);
	{
		const auto * current = gameHandler->gameState().getBattle(stunBattleID);
		if(!current || current != stunBattleOwner) return true;
		stack = current->battleGetUnitByID(stunUnitID);
		if(!stack)
		{
			if(stunnedActivation) completeStunnedActivation(*current, stunUnitID);
			return true;
		}
	}
	if(stunnedActivation)
	{
		const auto * current = gameHandler->gameState().getBattle(stunBattleID);
		if(!current || current != stunBattleOwner || !current->battleGetUnitByID(stunUnitID)) return true;
		clearReducedActivation(*current, stunUnitID);
		current = gameHandler->gameState().getBattle(stunBattleID);
		if(!current || current != stunBattleOwner || !current->battleGetUnitByID(stunUnitID)) return true;
		completeStunnedActivation(*current, stunUnitID);
		return true;
	}
#ifdef NH_OWNED_PREPARATION_SIMULATION_AUDIT
	// Activation effects can finalize the native battle. Reacquire both owners
	// instead of passing a potentially retired callback/actor to the test hook.
	if(const auto * current = gameHandler->gameState().getBattle(auditBattleID))
	{
		if(const auto * actor = current->battleGetUnitByID(auditUnitID))
			ownedPreparationAudit::afterActivation(*gameHandler, *current, *actor);
	}
#endif
	return false;
}
