/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "Client.h"
#include "CPlayerInterface.h"
#include "GameInstance.h"
#include "../lib/CConfigHandler.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/battle/SideInBattle.h"
#include "../lib/callback/CBattleGameInterface.h"
#include <utility>

template<typename T, typename ... Args, typename ... Args2>
void callOnlyThatBattleInterface(CClient & cl, PlayerColor player, void (T::*ptr)(Args...), Args2 && ...args)
{
	if(vstd::contains(cl.battleints,player))
		((*cl.battleints[player]).*ptr)(std::forward<Args2>(args)...);

	if(cl.additionalBattleInts.count(player))
	{
		for(auto bInt : cl.additionalBattleInts[player])
			((*bInt).*ptr)(std::forward<Args2>(args)...);
	}
}

template<typename T, typename ... Args, typename ... Args2>
void callBattleInterfaceIfPresentForBothSides(CClient & cl, const BattleID & battleID, void (T::*ptr)(Args...), Args2 && ...args)
{
	assert(cl.gameState().getBattle(battleID));

	if(!cl.gameState().getBattle(battleID))
	{
		logGlobal->error("Attempt to call battle interface without ongoing battle!");
		return;
	}

	callOnlyThatBattleInterface(cl, cl.gameState().getBattle(battleID)->getSide(BattleSide::ATTACKER).color, ptr, std::forward<Args2>(args)...);
	callOnlyThatBattleInterface(cl, cl.gameState().getBattle(battleID)->getSide(BattleSide::DEFENDER).color, ptr, std::forward<Args2>(args)...);
	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool() && GAME->interface()->battleInt)
	{
		callOnlyThatBattleInterface(cl, PlayerColor::SPECTATOR, ptr, std::forward<Args2>(args)...);
	}
}
