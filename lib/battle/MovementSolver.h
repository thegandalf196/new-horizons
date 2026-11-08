/*
 * MovementSolver.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <queue>

namespace battle::movement
{
/// Owned, normalized inputs to the path calculation. No world identities,
/// borrowed footprints, visibility queries or mutable game state survive here.
template<std::size_t Size>
struct Query
{
	std::int16_t startPosition = -1;
	std::uint8_t footprint = 0; // single, double attacker, double defender
	bool flying = false;
	std::bitset<Size> traversable;
	std::bitset<Size> stoppers;
	std::array<std::int32_t, Size> movementCost{};
	std::array<std::int8_t, Size> bypassCost{};

	bool operator==(const Query &) const = default;
};

template<std::size_t Size>
struct Paths
{
	static constexpr std::uint32_t infinite = 1000000;
	std::array<std::uint32_t, Size> distances;
	std::array<std::int16_t, Size> predecessors;

	Paths()
	{
		distances.fill(infinite);
		predecessors.fill(-1);
	}

	bool operator==(const Paths &) const = default;
};

/// Geometry is immutable and supplies the engine's existing ordered neighbours,
/// occupied footprints and direct hex distance. Negative footprint entries are
/// absent cells. Preserve FIFO relaxation and strict improvement: predecessors
/// and equal-cost route choices are observable, even when distances agree.
template<std::size_t Size, typename Geometry>
Paths<Size> solve(const Query<Size> & query, const Geometry & geometry)
{
	Paths<Size> result;
	if(query.flying)
	{
		for(std::size_t position = 0; position < Size; ++position)
			if(query.traversable.test(position))
			{
				result.predecessors[position] = query.startPosition;
				result.distances[position] = geometry.distance(query.startPosition, position);
			}
		return result;
	}
	if(query.startPosition < 0 || static_cast<std::size_t>(query.startPosition) >= Size)
		return result;

	const bool hasMovementCost = std::any_of(query.movementCost.begin(), query.movementCost.end(),
		[](auto cost) { return cost > 0; });
	std::queue<std::int16_t> pending;
	pending.push(query.startPosition);
	result.distances[query.startPosition] = 0;
	while(!pending.empty())
	{
		const auto current = pending.front();
		pending.pop();
		if(query.stoppers.test(current))
			continue;
		const auto currentFootprint = geometry.occupied(current, query.footprint);
		for(const auto neighbour : geometry.neighbours(current))
		{
			if(neighbour < 0)
				continue;
			std::int32_t additionalCost = query.bypassCost[neighbour];
			if(hasMovementCost)
			{
				for(const auto hex : geometry.occupied(neighbour, query.footprint))
					if(hex >= 0 && static_cast<std::size_t>(hex) < Size
						&& std::find(currentFootprint.begin(), currentFootprint.end(), hex) == currentFootprint.end())
						additionalCost += query.movementCost[hex];
			}
			const auto cost = result.distances[current] + static_cast<std::uint32_t>(1 + additionalCost);
			if(query.traversable.test(neighbour) && cost < result.distances[neighbour])
			{
				pending.push(neighbour);
				result.distances[neighbour] = cost;
				result.predecessors[neighbour] = current;
			}
		}
	}
	return result;
}
}
