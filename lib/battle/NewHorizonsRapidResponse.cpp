/*
 * NewHorizonsRapidResponse.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsRapidResponse.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "NewHorizonsFrozen.h"
#include "NewHorizonsSwiftRebirth.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsRapidResponse
{
bool eligible(const CBattleInfoCallback & battle, BattleSide side, const battle::Unit * unit)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	const auto * hero = battle.getBattle()->getSideHero(side);
	return hero && hero->hasActivePerk("new-horizons:battlecraft", std::string(PERK_KEY))
		&& unit && unit->alive() && !unit->isGhost() && unit->waited()
		&& unit->willMove() && !unit->isTimeStopped() && !newHorizonsFrozen::isFrozen(*unit)
		&& !newHorizonsSwiftRebirth::normalActivationCompleted(*unit, battle.battleGetRound())
		&& battle.battleGetOwner(unit) == battle.getBattle()->getSidePlayer(side);
}

const battle::Unit * latestWaiter(const CBattleInfoCallback & battle, BattleSide side)
{
	std::vector<battle::Units> turns;
	// Never let our own pending override influence which original queue slot is latest.
	battle.battleGetTurnOrder(turns, 0, 1, 0, BattleSide::NONE, false, false);
	if(turns.empty())
		return nullptr;
	for(auto it = turns.front().rbegin(); it != turns.front().rend(); ++it)
		if(eligible(battle, side, *it))
			return *it;
	return nullptr;
}

const battle::Unit * pendingWaiter(const CBattleInfoCallback & battle, BattleSide side)
{
	const auto & state = battle.getBattle()->getRapidResponseState(side);
	if(!state.enabled || state.pendingRound != battle.battleGetRound()
		|| state.lastUsedRound == battle.battleGetRound())
		return nullptr;
	const auto * unit = battle.battleGetUnitByID(state.pendingUnitId);
	return eligible(battle, side, unit) ? unit : nullptr;
}

RapidResponseState capture(const CBattleInfoCallback & battle, BattleSide side)
{
	auto state = battle.getBattle()->getRapidResponseState(side);
	if(!state.enabled || state.lastUsedRound == battle.battleGetRound()
		|| (state.pending() && state.pendingRound == battle.battleGetRound()))
		return state;
	state.clearPending();
	if(const auto * unit = latestWaiter(battle, side))
	{
		state.pendingRound = battle.battleGetRound();
		state.pendingUnitId = unit->unitId();
	}
	return state;
}

RapidResponseState resolve(const CBattleInfoCallback & battle, BattleSide side, bool consume)
{
	auto state = battle.getBattle()->getRapidResponseState(side);
	if(consume)
	{
		if(!pendingWaiter(battle, side))
			throw std::runtime_error("Cannot consume an invalid Rapid Response delayed activation");
		state.lastUsedRound = battle.battleGetRound();
	}
	state.clearPending();
	return state;
}
}
