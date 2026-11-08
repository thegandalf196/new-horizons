/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleMovementQuery.h"
#include "Unit.h"
#include <queue>

// Preserve the native prepared-world solver for callers without reuse and for
// cheap queries. Its FIFO order and strict improvements define route ties.
battle::MovementPaths battle::solveMovementDirect(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & obstacles,
	EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts)
{
	if(params.flying)
		return solveFlyingMovementDirect(accessibility, params);
	battle::MovementPaths ret;
	if(!params.startPosition.isValid())
		return ret;
	const auto stopped = [&](BattleHex hex)
	{
		for(const auto & occupiedHex : battle::Unit::getHexes(hex, params.doubleWide, params.side))
		{
			if(params.knownAccessible->contains(occupiedHex))
				continue;
			if(obstacles.contains(occupiedHex))
			{
				if(occupiedHex == BattleHex::GATE_BRIDGE)
				{
					if(gateState != EGateState::DESTROYED && params.side == BattleSide::ATTACKER)
						return true;
				}
				else
					return true;
			}
		}
		return false;
	};
	std::queue<BattleHex> hexq;
	hexq.push(params.startPosition);
	ret.distances[params.startPosition.toInt()] = 0;
	std::array<bool, GameConstants::BFIELD_SIZE> accessibleCache{};
	const bool hasMovementCost = std::any_of(movementCosts.begin(), movementCosts.end(),
		[](auto value) { return value > 0; });
	auto traversalAccessibility = accessibility;
	if(params.ghostWalk && !params.passThrough)
	{
		for(auto & tile : traversalAccessibility)
			if(tile == EAccessibility::ALIVE_STACK)
				tile = EAccessibility::ACCESSIBLE;
	}
	else if(!params.passThrough)
	{
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
			if(params.friendlyTransit.test(static_cast<std::size_t>(hex))
				&& traversalAccessibility[hex] == EAccessibility::ALIVE_STACK)
				traversalAccessibility[hex] = EAccessibility::ACCESSIBLE;
	}
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
	{
		const BattleHex position(static_cast<si16>(hex));
		accessibleCache[hex] = params.passThrough
			? traversalAccessibility.accessibleForPassThroughTransit(position, params.doubleWide, params.side)
			: traversalAccessibility.accessible(position, params.doubleWide, params.side);
	}
	while(!hexq.empty())
	{
		const BattleHex curHex = hexq.front();
		hexq.pop();
		if(stopped(curHex))
			continue;
		const auto * before = hasMovementCost
			? &battle::Unit::getHexes(curHex, params.doubleWide, params.side) : nullptr;
		for(const BattleHex & neighbour : curHex.getNeighbouringTiles())
		{
			int additionalCost = 0;
			if(hasMovementCost)
			{
				const auto & after = battle::Unit::getHexes(neighbour, params.doubleWide, params.side);
				for(const auto & hex : after)
					if(hex.isValid() && !before->contains(hex))
						additionalCost += movementCosts[hex.toInt()];
			}
			if(params.bypassEnemyStacks && !params.passThrough)
			{
				const auto enemy = params.destructibleEnemyTurns.at(neighbour.toInt());
				if(enemy >= 0)
					additionalCost += enemy;
			}
			const auto cost = ret.distances.at(curHex.toInt()) + static_cast<std::uint32_t>(1 + additionalCost);
			if(accessibleCache[neighbour.toInt()] && cost < ret.distances[neighbour.toInt()])
			{
				hexq.push(neighbour);
				ret.distances[neighbour.toInt()] = cost;
				ret.predecessors[neighbour.toInt()] = curHex.toInt();
			}
		}
	}
	return ret;
}
