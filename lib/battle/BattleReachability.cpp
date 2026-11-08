/*
 * BattleReachability.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "CObstacleInstance.h"
#include "Unit.h"

namespace
{
void applyPaths(ReachabilityInfo & result, const battle::MovementPaths & paths)
{
	static_assert(ReachabilityInfo::INFINITE_DIST == battle::MovementPaths::infinite);
	result.distances = paths.distances;
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		result.predecessors[hex] = BattleHex(paths.predecessors[hex]);
}

}

battle::MovementPaths CBattleInfoCallback::movementPaths(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers,
	EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts) const
{
	return battle::solveMovementDirect(accessibility, params, stoppers, gateState, movementCosts);
}

ReachabilityInfo CBattleInfoCallback::makeBFS(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params) const
{
	ReachabilityInfo ret;
	ret.accessibility = accessibility;
	ret.params = params;
	if(!params.startPosition.isValid()) // Arrow turrets do not walk.
		return ret;

	const BattleHexArray obstacles = getStoppers(params.perspective);
	std::array<std::int32_t, GameConstants::BFIELD_SIZE> movementCosts{};
	for(const auto & obstacle : battleGetAllObstacles(params.perspective))
	{
		const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
		if(!spellObstacle || spellObstacle->turnsRemaining == 0 || spellObstacle->movementCost <= 0)
			continue;
		for(const auto & hex : spellObstacle->getAffectedTiles())
			if(hex.isValid())
				movementCosts[hex.toInt()] = std::max(movementCosts[hex.toInt()], spellObstacle->movementCost);
	}
	const auto gateState = obstacles.contains(BattleHex::GATE_BRIDGE)
		&& params.side == BattleSide::ATTACKER ? battleGetGateState() : EGateState::NONE;
	applyPaths(ret, movementPaths(accessibility, params, obstacles, gateState, movementCosts));
	return ret;
}

ReachabilityInfo CBattleInfoCallback::getFlyingReachability(const ReachabilityInfo::Parameters & params) const
{
	ReachabilityInfo ret;
	ret.accessibility = getAccessibility(*params.knownAccessible);
	ret.params = params;
	// Flying is a cheap linear scan. Compiling and caching it regresses cold
	// queries, so retain the native calculation without cache admission overhead.
	battle::fillFlyingMovementPaths(ret.accessibility, params, ret.distances, ret.predecessors);
	return ret;
}

ReachabilityInfo CBattleInfoCallback::getReachability(const ReachabilityInfo::Parameters & params) const
{
	if(params.flying)
		return getFlyingReachability(params);
	else
	{
		auto accessibility = getAccessibility(* params.knownAccessible);

		accessibility.destructibleEnemyTurns = std::make_shared<const TBattlefieldTurnsArray>(
			params.destructibleEnemyTurns);

		return makeBFS(accessibility, params);
	}
}

BattleHexArray CBattleInfoCallback::getStoppers(BattleSide whichSidePerspective) const
{
	BattleHexArray ret;
	RETURN_IF_NOT_BATTLE(ret);

	for(auto &oi : battleGetAllObstacles(whichSidePerspective))
	{
		if(!battleIsObstacleVisibleForSide(*oi, whichSidePerspective))
			continue;

		for(const auto & hex : oi->getStoppingTile())
		{
			if(hex == BattleHex::GATE_BRIDGE && oi->obstacleType == CObstacleInstance::MOAT)
			{
				if(battleGetGateState() == EGateState::OPENED || battleGetGateState() == EGateState::DESTROYED)
					continue; // this tile is disabled by drawbridge on top of it
			}
			ret.insert(hex);
		}
	}
	return ret;
}
