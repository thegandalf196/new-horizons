/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsDemonicGating.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

bool BattleActionProcessor::doDemonicGatingAction(const CBattleInfoCallback & battle, const BattleAction & ba)
{
	const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle());
	const auto * actingStack = battle.battleGetStackByID(ba.stackNumber, false);
	if(!concrete || !canStackAct(battle, actingStack))
		return false;
	if(ba.target.size() == 2)
	{
		processBattleEventTriggers(battle, CombatEventType::BEFORE_MOVE, actingStack, nullptr);
		const auto movement = moveStack(battle, ba.stackNumber, ba.target.front().hexValue);
		if(movement.invalidRequest)
			return false;
		const auto * source = battle.battleGetStackByID(ba.stackNumber, false);
		if(source && source->alive())
			processBattleEventTriggers(battle, CombatEventType::AFTER_MOVE, source, nullptr);
		const auto * hero = battle.battleGetFightingHero(ba.side);
		if(newHorizonsDemonicGating::usesTemporaryGating(hero)
			&& (!source || source->getPosition() != ba.target.front().hexValue))
			return true; // An interrupted new-rule move never opens or spends a Gate.
		BattleAction arrival = ba;
		arrival.target.erase(arrival.target.begin());
		const auto planned = newHorizonsDemonicGating::plan(battle, arrival);
		if(!planned)
			return true; // Movement/reactions consumed the activation, but no Gate was committed.
		const auto & side = concrete->getSide(ba.side);
		BattleDemonicGatingStateChanged update;
		update.battleID = concrete->getBattleID();
		update.side = ba.side;
		update.reserve = side.demonicReserve;
		update.pending = side.pendingDemonicGates;
		update.gated = side.gatedDemonicStacks;
		update.chainGateArmed = side.chainGateArmed;
		update.masterGateUsed = side.masterGateUsed;
		SideInBattle::PendingDemonicGate gate;
		gate.creature = planned->creature;
		gate.count = planned->count;
		gate.position = planned->position;
		// Chain Gate spending remains the following existing ordered transition.
		gate.arrivalRound = concrete->getRound() + 1;
		gate.sourceUnitId = planned->sourceUnitId;
		gate.temporary = planned->temporary;
		if(!gate.temporary)
			update.reserve.erase(gate.creature);
		update.pending.push_back(gate);
		if(planned->freeActivation)
		{
			update.masterGateUsed = true;
			update.masterGateContinuationUnitId = ba.stackNumber;
		}
		gameHandler->sendAndApply(update);
	}
	const auto & gates = concrete->getSide(ba.side).pendingDemonicGates;
	const auto found = std::ranges::find_if(gates, [&ba](const auto & gate)
	{
		return gate.sourceUnitId == ba.stackNumber && gate.creature == ba.gatingCreature;
	});
	if(found == gates.end())
		return false;
	const auto acceptedGate = *found;

	// A valid Gate is the only point at which the token is spent.  Validation
	// happened before StartAction, so rejected requests never reach this block;
	// Swift Gate still consumes Chain Gate but retains its already-end-of-round
	// timing instead of advancing it any further.
	const auto * hero = battle.battleGetFightingHero(ba.side);
	if(hero && hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.chainGate")
		&& concrete->getChainGateArmed(ba.side))
	{
		const bool swiftGate = hero->hasActivePerk(
			"new-horizons:demonicGating", "new-horizons:demonicGating.swiftGate");
		BattleDemonicGatingStateChanged update;
		update.battleID = concrete->getBattleID();
		update.side = ba.side;
		update.reserve = concrete->getSide(ba.side).demonicReserve;
		update.pending = concrete->getSide(ba.side).pendingDemonicGates;
		update.gated = concrete->getSide(ba.side).gatedDemonicStacks;
		update.chainGateArmed = false;
		update.masterGateUsed = concrete->getSide(ba.side).masterGateUsed;
		if(!swiftGate)
		{
			const auto pending = std::ranges::find_if(update.pending, [&ba](const auto & gate)
			{
				return gate.sourceUnitId == ba.stackNumber && gate.creature == ba.gatingCreature;
			});
			if(pending != update.pending.end())
			{
				pending->arrivalRound = concrete->getRound();
				pending->chainGateAccelerated = true;
			}
		}
		gameHandler->sendAndApply(update);
	}
	BattleLogMessage message;
	message.battleID = concrete->getBattleID();
	MetaString line = MetaString::createFromRawString("A Gate opens for ");
	line.appendNumber(acceptedGate.count);
	line.appendRawString(" ");
	line.appendName(acceptedGate.creature, acceptedGate.count);
	line.appendRawString(".");
	message.lines.push_back(std::move(line));
	gameHandler->sendAndApply(message);
	return true;
}
