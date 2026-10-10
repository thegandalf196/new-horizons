/*
 * NewHorizonsOpportunist.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsOpportunist.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "NewHorizonsFrozen.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>

namespace newHorizonsOpportunist
{
bool hasPerk(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	if(!unit)
		return false;
	const auto side = battle.playerToSide(battle.battleGetActionController(unit));
	const auto * hero = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
		? battle.battleGetFightingHero(side) : nullptr;
	return hero && hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.opportunist");
}

int movementAllowance(const CBattleInfoCallback & battle, const battle::Unit * unit, int remainingMovement)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	if(!state || !state->luckyOwnAttackSequence || !hasPerk(battle, unit)
		|| !unit->alive() || unit->isGhost() || !unit->canMove() || unit->isTimeStopped()
		|| newHorizonsFrozen::isFrozen(*unit) || state->armorerLastStandEndedActivation)
		return 0;
	return std::min(MAX_MOVEMENT, std::max(0, remainingMovement));
}
}
