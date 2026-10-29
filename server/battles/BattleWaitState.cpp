/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleWaitState.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsForestAmbush.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleWaitState
{
void accept(CGameHandler & game, const CBattleInfoCallback & battle, const CStack & stack)
{
	if(newHorizonsBattlecraft::hasOverwatch(battle.battleGetOwnerHero(&stack))
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&stack) && stack.isShooter()
		&& stack.battlecraftOverwatchUsedRound != battle.battleGetRound())
	{
		auto state = stack.acquireState();
		state->battlecraftOverwatchReadyRound = battle.battleGetRound();
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		UnitChanges update(stack.unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		changed.changedStacks.push_back(std::move(update));
		game.sendAndApply(changed);
	}

	if(newHorizonsForestAmbush::hasBattlePolicy(battle))
	{
		auto state = stack.acquireState();
		newHorizonsForestAmbush::acceptWait(battle, *state);
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		UnitChanges update(stack.unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		changed.changedStacks.push_back(std::move(update));
		game.sendAndApply(changed);
	}
}
}
