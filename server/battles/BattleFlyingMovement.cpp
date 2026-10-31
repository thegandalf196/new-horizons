/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "BattleVoluntaryMovementHistory.h"
#include "GateDrawbridgeFootprint.h"
#include "../../lib/battle/NewHorizonsLivingInferno.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/CStack.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../CGameHandler.h"
bool BattleActionProcessor::commitFlyingMovement(const CBattleInfoCallback & battle, const CStack & unit,
	const FlightMovementInput & input, newHorizonsLivingInferno::Trail & trail, MovementResult & result)
{
	if(input.distance > input.movementRange || input.path.empty())
		return true;
	if(input.canUseGate && input.gateState != EGateState::OPENED
		&& gateDrawbridgeFootprintContains(unit, input.destination, input.hasWideMoat))
	{
		BattleUpdateGateState gate;
		gate.battleID = battle.getBattle()->getBattleID();
		gate.state = EGateState::OPENED;
		gameHandler->sendAndApply(gate);
	}
	BattleStackMoved movement;
	movement.battleID = battle.getBattle()->getBattleID();
	movement.stack = unit.unitId();
	BattleHexArray tiles;
	tiles.insert(input.path[0]);
	movement.tilesToMove = tiles;
	movement.distance = input.distance;
	movement.teleporting = false;
	breakSanctuary(battle, &unit);
	gameHandler->sendAndApply(movement);
	result.destinationCommitted |= unit.getPosition() == input.destination;
	result.voluntaryFlightDistance = input.voluntary && result.destinationCommitted ? input.distance : 0;
	battleVoluntaryMovementHistory::record(*gameHandler, battle, unit, input.voluntary, input.start, unit.getPosition());
	trail.traverse(input.start, movement.tilesToMove, true);
	result.distance = input.distance;
	result.movementCost = input.distance;
	if(input.voluntary)
		resolveOverwatch(battle, &unit, input.start, unit.getPosition());
	if(!unit.alive())
		return false;
	const auto state = unit.acquireState();
	return !state || !state->armorerLastStandEndedActivation;
}
