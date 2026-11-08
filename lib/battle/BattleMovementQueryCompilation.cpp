/*
 * BattleMovementQueryCompilation.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleMovementQuery.h"
#include "Unit.h"

namespace
{
battle::MovementQuery movementQuery(const ReachabilityInfo::Parameters & params)
{
	battle::MovementQuery query;
	query.startPosition = params.startPosition.toInt();
	query.footprint = params.doubleWide ? (params.side == BattleSide::ATTACKER ? 1 : 2) : 0;
	query.flying = params.flying;
	return query;
}

AccessibilityInfo traversalMap(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params)
{
	auto traversal = accessibility;
	if(params.passThrough)
		return traversal;
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		if(traversal[hex] == EAccessibility::ALIVE_STACK
			&& (params.ghostWalk || params.friendlyTransit.test(static_cast<std::size_t>(hex))))
			traversal[hex] = EAccessibility::ACCESSIBLE;
	return traversal;
}

void compileStoppers(battle::MovementQuery & query, const ReachabilityInfo::Parameters & params,
	const BattleHexArray & obstacles, EGateState gateState)
{
	const auto inverseSide = params.side == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
	for(const auto hex : obstacles)
	{
		// The walking solver always permits leaving its original footprint.
		if(params.knownAccessible->contains(hex))
			continue;
		if(hex == BattleHex::GATE_BRIDGE
			&& (gateState == EGateState::DESTROYED || params.side != BattleSide::ATTACKER))
			continue;
		// Inverting the physical footprint gives every anchor covering this
		// stopping tile. This avoids scanning all battlefield anchors per query.
		for(const auto anchor : battle::Unit::getHexes(hex, params.doubleWide, inverseSide))
			query.stoppers.set(anchor.toInt());
	}
}
}

battle::MovementQuery battle::compileWalkingMovementQuery(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers, EGateState gateState,
	const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts)
{
	auto query = movementQuery(params);
	query.movementCost = movementCosts;
	compileStoppers(query, params, stoppers, gateState);
	const auto traversal = traversalMap(accessibility, params);
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
	{
		const BattleHex position(static_cast<si16>(hex));
		query.traversable.set(hex, params.passThrough
			? traversal.accessibleForPassThroughTransit(position, params.doubleWide, params.side)
			: traversal.accessible(position, params.doubleWide, params.side));
		if(params.bypassEnemyStacks && !params.passThrough)
			query.bypassCost[hex] = std::max<std::int8_t>(0, params.destructibleEnemyTurns[hex]);
	}
	return query;
}

battle::MovementQuery battle::compileFlyingMovementQuery(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params)
{
	auto query = movementQuery(params);
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		query.traversable.set(hex, accessibility.accessible(hex, params.doubleWide, params.side));
	return query;
}
