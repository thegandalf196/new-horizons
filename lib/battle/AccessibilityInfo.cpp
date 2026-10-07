/*
 * AccessibilityInfo.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AccessibilityInfo.h"
#include "BattleHex.h"
#include "Unit.h"
#include "../GameConstants.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
struct CubeCoordinates
{
	double x;
	double y;
	double z;
};

CubeCoordinates toCubeCoordinates(const BattleHex & hex)
{
	const int row = hex.getY();
	const int axialQ = hex.getX() + row / 2;
	const int axialR = row;
	return {static_cast<double>(axialQ), static_cast<double>(axialR - axialQ), static_cast<double>(-axialR)};
}

BattleHex fromCubeCoordinates(int x, int z)
{
	const int row = -z;
	const int column = x - row / 2;
	BattleHex result;
	result.setXY(static_cast<si16>(column), static_cast<si16>(row), false);
	return result;
}

std::vector<BattleHex> nearestHexesOnRay(const CubeCoordinates & point)
{
	const std::array<int, 2> xValues = {static_cast<int>(std::floor(point.x)), static_cast<int>(std::ceil(point.x))};
	const std::array<int, 2> yValues = {static_cast<int>(std::floor(point.y)), static_cast<int>(std::ceil(point.y))};
	const std::array<int, 2> zValues = {static_cast<int>(std::floor(point.z)), static_cast<int>(std::ceil(point.z))};
	double bestError = std::numeric_limits<double>::infinity();
	std::vector<BattleHex> result;
	constexpr double epsilon = 1e-9;

	for(const int x : xValues)
	for(const int y : yValues)
	for(const int z : zValues)
	{
		if(x + y + z != 0)
			continue;

		const double dx = point.x - x;
		const double dy = point.y - y;
		const double dz = point.z - z;
		const double error = dx * dx + dy * dy + dz * dz;
		const BattleHex candidate = fromCubeCoordinates(x, z);
		if(!candidate.isAvailable())
			continue;

		if(error + epsilon < bestError)
		{
			bestError = error;
			result.clear();
			result.push_back(candidate);
		}
		else if(std::abs(error - bestError) <= epsilon
			&& std::find(result.begin(), result.end(), candidate) == result.end())
		{
			result.push_back(candidate);
		}
	}
	return result;
}
}

bool AccessibilityInfo::tileAccessibleWithGate(const BattleHex & tile, BattleSide side, uint32_t ignoredGateReservations,
	bool allowDestructibleEnemyTurns) const
{
	if(!tile.isValid())
		return false;

	const auto index = static_cast<size_t>(tile.toInt());
	if(demonicGateReservationCounts[index] > ignoredGateReservations)
		return false;

	auto accessibility = at(index);
	if(accessibility == EAccessibility::DEMONIC_GATE_RESERVED && ignoredGateReservations > 0)
		accessibility = demonicGateReservationBase[index];

	if(accessibility == EAccessibility::ALIVE_STACK)
	{
		if(!allowDestructibleEnemyTurns || !destructibleEnemyTurns)
			return false;

		return destructibleEnemyTurns->at(tile.toInt()) >= 0;
	}

	if(accessibility != EAccessibility::ACCESSIBLE)
		if(accessibility != EAccessibility::GATE || side != BattleSide::DEFENDER)
			return false;

	return true;
}

bool AccessibilityInfo::accessible(const BattleHex & tile, const battle::Unit * stack) const
{
	return accessible(tile, stack->doubleWide(), stack->unitSide());
}

bool AccessibilityInfo::accessible(const BattleHex & tile, bool doubleWide, BattleSide side) const
{
	return accessibleImpl(tile, doubleWide, side, BattleHex::INVALID, false);
}

bool AccessibilityInfo::accessibleForPassThroughTransit(const BattleHex & tile, bool doubleWide, BattleSide side) const
{
	const auto canTraverse = [this](const BattleHex & hex)
	{
		if(!hex.isAvailable() || isDemonicGateReserved(hex))
			return false;

		switch(at(static_cast<size_t>(hex.toInt())))
		{
		case EAccessibility::ACCESSIBLE:
		case EAccessibility::ALIVE_STACK:
		case EAccessibility::OBSTACLE:
		case EAccessibility::DESTRUCTIBLE_WALL:
		case EAccessibility::GATE:
			return true;

		case EAccessibility::UNAVAILABLE:
		case EAccessibility::SIDE_COLUMN:
		case EAccessibility::DEMONIC_GATE_RESERVED:
			return false;
		}

		return false;
	};

	if(!tile.isAvailable() || !canTraverse(tile))
		return false;

	if(!doubleWide)
		return true;

	const auto otherHex = battle::Unit::occupiedHex(tile, doubleWide, side);
	return otherHex.isAvailable() && canTraverse(otherHex);
}

bool AccessibilityInfo::accessibleForMovementEndpoint(const BattleHex & tile, bool doubleWide, BattleSide side) const
{
	return accessibleImpl(tile, doubleWide, side, BattleHex::INVALID, false, false);
}

bool AccessibilityInfo::hasClearStraightHexRay(const BattleHex & from, const BattleHex & to,
	const BattleHexArray & allowedOccupiedHexes) const
{
	if(!from.isAvailable() || !to.isAvailable())
		return false;

	const auto distance = BattleHex::getDistance(from, to);
	if(distance == 0)
		return true;

	const auto start = toCubeCoordinates(from);
	const auto finish = toCubeCoordinates(to);
	std::vector<std::vector<BattleHex>> rayLayers(static_cast<size_t>(distance) + 1);
	rayLayers.front().push_back(from);
	rayLayers.back().push_back(to);

	for(size_t step = 1; step < distance; ++step)
	{
		const double fraction = static_cast<double>(step) / distance;
		const CubeCoordinates point{
			start.x + (finish.x - start.x) * fraction,
			start.y + (finish.y - start.y) * fraction,
			start.z + (finish.z - start.z) * fraction};
		rayLayers[step] = nearestHexesOnRay(point);
		if(rayLayers[step].empty())
			return false;
	}

	const auto intermediateCellClear = [&](const BattleHex & hex)
	{
		if(hex == from || hex == to)
			return true;

		const auto accessibility = at(static_cast<size_t>(hex.toInt()));
		return accessibility == EAccessibility::ACCESSIBLE
			|| (accessibility == EAccessibility::ALIVE_STACK && allowedOccupiedHexes.contains(hex));
	};

	auto hasClearPath = [&](auto && self, size_t layer, const BattleHex & current) -> bool
	{
		if(layer + 1 == rayLayers.size())
			return current == to;

		for(const auto & candidate : rayLayers[layer + 1])
		{
			if(BattleHex::getDistance(current, candidate) != 1)
				continue;
			if(candidate != to && !intermediateCellClear(candidate))
				continue;
			if(self(self, layer + 1, candidate))
				return true;
		}
		return false;
	};

	return hasClearPath(hasClearPath, 0, from);
}

std::optional<BattleHex> AccessibilityInfo::nearestLegalPosition(const BattleHex & origin, bool doubleWide, BattleSide side) const
{
	if(!origin.isAvailable() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return std::nullopt;

	// Reachability may allow walking through an enemy that the AI expects to
	// destroy first. A placement effect cannot make that prediction: the hex
	// must already be vacant at the moment of relocation.
	AccessibilityInfo placement = *this;
	placement.destructibleEnemyTurns.reset();
	if(placement.accessible(origin, doubleWide, side))
		return origin;
	std::optional<BattleHex> nearest;
	int nearestDistance = std::numeric_limits<int>::max();
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(static_cast<si16>(index));
		if(!candidate.isAvailable() || !placement.accessible(candidate, doubleWide, side))
			continue;

		const int distance = BattleHex::getDistance(origin, candidate);
		if(distance < nearestDistance)
		{
			nearest = candidate;
			nearestDistance = distance;
		}
	}
	return nearest;
}

bool AccessibilityInfo::accessibleForDemonicGateArrival(const BattleHex & tile, bool doubleWide, BattleSide side,
	const BattleHex & reservedPosition, bool reservedDoubleWide) const
{
	return accessibleImpl(tile, doubleWide, side, reservedPosition, reservedDoubleWide);
}

bool AccessibilityInfo::accessibleImpl(const BattleHex & tile, bool doubleWide, BattleSide side,
	const BattleHex & ignoredReservationPosition, bool ignoredReservationDoubleWide, bool allowDestructibleEnemyTurns) const
{
	// All hexes that stack would cover if standing on tile have to be accessible.
	//do not use getHexes for speed reasons
	if(!tile.isValid())
		return false;

	const BattleHex ignoredReservationOtherHex = ignoredReservationPosition.isValid()
		? battle::Unit::occupiedHex(ignoredReservationPosition, ignoredReservationDoubleWide, side)
		: BattleHex::INVALID;
	auto ignoredReservationCount = [this, &ignoredReservationPosition, &ignoredReservationOtherHex](const BattleHex & hex)
	{
		const bool ownFootprint = hex == ignoredReservationPosition || hex == ignoredReservationOtherHex;
		return ownFootprint && isDemonicGateReserved(hex) ? 1u : 0u;
	};

	if(!tileAccessibleWithGate(tile, side, ignoredReservationCount(tile), allowDestructibleEnemyTurns))
		return false;

	if(doubleWide)
	{
		auto otherHex = battle::Unit::occupiedHex(tile, doubleWide, side);
		if(!otherHex.isValid())
			return false;
		if(!tileAccessibleWithGate(otherHex, side, ignoredReservationCount(otherHex), allowDestructibleEnemyTurns))
			return false;
	}

	return true;
}

void AccessibilityInfo::reserveDemonicGateFootprint(const BattleHex & position, bool doubleWide, BattleSide side)
{
	if(!position.isAvailable())
		return;

	for(const auto & hex : battle::Unit::getHexes(position, doubleWide, side))
	{
		if(!hex.isAvailable())
			continue;

		const auto index = static_cast<size_t>(hex.toInt());
		if(demonicGateReservationCounts[index] == 0)
		{
			demonicGateReservationBase[index] = at(index);
			if(at(index) == EAccessibility::ACCESSIBLE || at(index) == EAccessibility::GATE)
				at(index) = EAccessibility::DEMONIC_GATE_RESERVED;
		}
		if(demonicGateReservationCounts[index] < std::numeric_limits<uint32_t>::max())
			++demonicGateReservationCounts[index];
	}
}

bool AccessibilityInfo::isDemonicGateReserved(const BattleHex & tile) const
{
	return tile.isAvailable() && demonicGateReservationCounts[static_cast<size_t>(tile.toInt())] > 0;
}
