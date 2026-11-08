/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleFlowProcessor.h"
#include "BattleProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/SetStackEffect.h"
void BattleFlowProcessor::completeStunnedActivation(const CBattleInfoCallback & battle, uint32_t unitId)
{
	// setActiveStack already published the ACTUAL typed activation reason and
	// ran its start effects exactly once. Do not begin an AUTOMATIC activation
	// on top: Morale, Second Wind and reduced extras retain their native clocks.
	const auto battleID = battle.getBattle()->getBattleID();
	auto * original = gameHandler->gs->getBattle(battleID);
	const auto clearSecondWind = [this, unitId](BattleInfo & current)
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			if(const auto order = current.getHeroOrderState(side, HeroCommand::SECOND_WIND);
				order && order->secondWindActive && order->primaryTargetUnitId == unitId)
			{
				current.setHeroOrderSecondWindActive(side, false);
				publishHeroOrderState(current, side);
			}
	};
	const auto * unit = original ? original->battleGetStackByID(unitId, false) : nullptr;
	if(unit && !unit->alive())
	{
		// The actual activation began, but its start effects killed the body.
		// Its already-spent slot cannot leave a future Stun after resurrection.
		const auto removed = newHorizonsStun::removalPlan(*unit);
		if(!removed.empty())
		{
			SetStackEffect effect;
			effect.battleID = battleID;
			effect.toRemove.emplace_back(unitId, removed);
			gameHandler->sendAndApply(effect);
		}
		auto * current = gameHandler->gs->getBattle(battleID);
		if(current == original)
		{
			clearSecondWind(*current);
			if(!owner->checkBattleStateChanges(*current)) activateNextStack(*current);
		}
		return;
	}
	if(!unit || !unit->alive() || unit->isTimeStopped())
	{
		if(original)
		{
			clearSecondWind(*original);
			if(!owner->checkBattleStateChanges(*original)) activateNextStack(*original);
		}
		return;
	}
	const auto removed = newHorizonsStun::removalPlan(*unit);
	if(removed.empty()) throw std::runtime_error("Stun activation lost its pending marker");
	if(!owner->makeAutomaticBattleAction(battle, BattleAction::makeNoAction(unit)))
		throw std::runtime_error("Failed to consume a stunned Creature Activation");
	auto * current = gameHandler->gs->getBattle(battleID);
	if(current != original || owner->checkBattleStateChanges(*current)) return;
	newHorizonsPlague::completeActivation(*current, *gameHandler->spellcastEnvironment(), unitId);
	current = gameHandler->gs->getBattle(battleID);
	if(current != original || owner->checkBattleStateChanges(*current)) return;
	if(current->battleGetUnitByID(unitId))
	{
		SetStackEffect effect;
		effect.battleID = battleID;
		effect.toRemove.emplace_back(unitId, removed);
		gameHandler->sendAndApply(effect);
	}
	clearSecondWind(*current);
	activateNextStack(*current);
}
