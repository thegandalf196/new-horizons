/*
 * BattleMovementQuery.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "BattleHex.h"
#include "MovementSolver.h"
#include "ReachabilityInfo.h"

namespace battle
{
using MovementQuery = movement::Query<GameConstants::BFIELD_SIZE>;
using MovementPaths = movement::Paths<GameConstants::BFIELD_SIZE>;

/// Fill an already initialized result with the original flying calculation.
/// Inaccessible cells keep their infinite-distance/invalid-predecessor defaults.
/// Supports both owned integer paths and ReachabilityInfo's BattleHex output,
/// avoiding a second result initialization and conversion in the live callback.
template<typename Predecessor>
void fillFlyingMovementPaths(const AccessibilityInfo & accessibility, const ReachabilityInfo::Parameters & params,
	std::array<std::uint32_t, GameConstants::BFIELD_SIZE> & distances,
	std::array<Predecessor, GameConstants::BFIELD_SIZE> & predecessors)
{
	for(int i = 0; i < GameConstants::BFIELD_SIZE; ++i)
		if(accessibility.accessible(i, params.doubleWide, params.side))
		{
			predecessors[i] = params.startPosition.toInt();
			distances[i] = BattleHex::getDistance(params.startPosition, i);
		}
}

inline MovementPaths solveFlyingMovementDirect(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params)
{
	MovementPaths result;
	fillFlyingMovementPaths(accessibility, params, result.distances, result.predecessors);
	return result;
}

DLL_LINKAGE MovementPaths solveMovementDirect(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers,
	EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts);
DLL_LINKAGE MovementPaths solveMovementQuery(const MovementQuery & query);
DLL_LINKAGE MovementQuery compileWalkingMovementQuery(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers, EGateState gateState,
	const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts);
DLL_LINKAGE MovementQuery compileFlyingMovementQuery(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params);
}
