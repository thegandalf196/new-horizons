/*
 * BattleMovementSolverTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/battle/MovementSolver.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int size = 37;
using Query = battle::movement::Query<size>;
using Paths = battle::movement::Paths<size>;

struct Geometry
{
	std::array<std::vector<std::int16_t>, size> edges;

	const auto & neighbours(std::int16_t position) const { return edges.at(position); }

	std::array<std::int16_t, 2> occupied(std::int16_t position, std::uint8_t footprint) const
	{
		const auto second = footprint == 1 ? position - 1 : position + 1;
		return {position, footprint && second >= 0 && second < size
			? static_cast<std::int16_t>(second) : static_cast<std::int16_t>(-1)};
	}

	std::uint32_t distance(std::int16_t from, std::int16_t to) const
	{
		return std::abs(static_cast<int>(from) - to);
	}
};

void require(bool success, const char * message)
{
	if(!success)
		throw std::runtime_error(message);
}

// Frozen FIFO algorithm from CBattleInfoCallback::makeBFS before extraction.
// Its vector cursor, explicit footprint comparisons and separate cost loops
// deliberately do not call any implementation helper from MovementSolver.
Paths legacy(const Query & query, const Geometry & geometry)
{
	Paths result;
	if(query.flying)
	{
		for(int i = 0; i < size; ++i)
			if(query.traversable[i])
			{
				result.predecessors[i] = query.startPosition;
				result.distances[i] = geometry.distance(query.startPosition, i);
			}
		return result;
	}
	if(query.startPosition < 0 || query.startPosition >= size)
		return result;
	std::vector<std::int16_t> queue{query.startPosition};
	result.distances[query.startPosition] = 0;
	for(std::size_t index = 0; index < queue.size(); ++index)
	{
		const auto current = queue[index];
		if(query.stoppers[current])
			continue;
		for(const auto next : geometry.edges[current])
		{
			int additionalCost = 0;
			const auto before = geometry.occupied(current, query.footprint);
			const auto after = geometry.occupied(next, query.footprint);
			for(const auto tile : after)
				if(tile >= 0 && tile < size && tile != before[0] && tile != before[1])
					additionalCost += query.movementCost[tile];
			additionalCost += query.bypassCost[next];
			const auto cost = result.distances[current] + static_cast<std::uint32_t>(1 + additionalCost);
			if(query.traversable[next] && cost < result.distances[next])
			{
				queue.push_back(next);
				result.distances[next] = cost;
				result.predecessors[next] = current;
			}
		}
	}
	return result;
}

void stableTiesAndWeightedRelaxation()
{
	Geometry geometry;
	geometry.edges[0] = {2, 1};
	geometry.edges[1] = {3};
	geometry.edges[2] = {3};
	geometry.edges[3] = {4};
	Query query;
	query.startPosition = 0;
	query.traversable.set();
	auto result = battle::movement::solve(query, geometry);
	require(result.distances[3] == 2 && result.predecessors[3] == 2, "FIFO equal-cost predecessor changed");
	query.movementCost[2] = 7;
	result = battle::movement::solve(query, geometry);
	require(result.distances[4] == 3 && result.predecessors[3] == 1, "Weighted route did not relax again");
	require(result == legacy(query, geometry), "Weighted reference mismatch");
	query.stoppers.set(3);
	result = battle::movement::solve(query, geometry);
	require(result.distances[3] == 2 && result.distances[4] == Paths::infinite, "Stopper must allow arrival but stop traversal");
}

void footprintAndBypassCosts()
{
	Geometry geometry;
	geometry.edges[7] = {8};
	geometry.edges[8] = {9};
	Query query;
	query.startPosition = 7;
	query.traversable.set();
	query.footprint = 1;
	query.movementCost[7] = 40;
	query.movementCost[8] = 4;
	query.movementCost[9] = 2;
	query.bypassCost[8] = 3;
	const auto result = battle::movement::solve(query, geometry);
	require(result.distances[8] == 8, "Overlapping double-wide footprint was charged twice");
	require(result.distances[9] == 11, "Entering footprint or bypass cost changed");
	query.footprint = 2;
	require(battle::movement::solve(query, geometry) == legacy(query, geometry), "Opposite wide footprint mismatch");
}

void invalidAndFlyingStarts()
{
	Geometry geometry;
	Query query;
	query.traversable.set(3);
	for(const auto start : {-4, -1, static_cast<int>(size)})
	{
		query.startPosition = start;
		require(battle::movement::solve(query, geometry) == Paths{}, "Invalid walking start must remain unreachable");
	}
	query.startPosition = -2;
	query.flying = true;
	query.stoppers.set();
	query.movementCost.fill(100);
	query.bypassCost.fill(50);
	const auto result = battle::movement::solve(query, geometry);
	require(result == legacy(query, geometry), "Flying projection changed");
	require(result.predecessors[3] == -2 && result.distances[3] == 5,
		"Flying start uses original direct-distance semantics");
	require(result.distances[2] == Paths::infinite, "Flying inaccessible endpoint became reachable");
}

void randomizedReferenceParity()
{
	std::mt19937 random(0x82a4c91u);
	for(int sample = 0; sample < 1200; ++sample)
	{
		Geometry geometry;
		Query query;
		query.startPosition = sample % 59 == 0 ? -1 : random() % size;
		query.footprint = random() % 3;
		query.flying = sample % 7 == 0;
		for(int i = 0; i < size; ++i)
		{
			query.traversable.set(i, random() % 5 != 0);
			query.stoppers.set(i, random() % 13 == 0);
			query.movementCost[i] = sample % 3 ? random() % 9 : 0;
			query.bypassCost[i] = sample % 4 ? random() % 12 : 0;
			for(int edge = 0; edge < 6; ++edge)
				geometry.edges[i].push_back(random() % size);
		}
		require(battle::movement::solve(query, geometry) == legacy(query, geometry),
			"Seeded graph/layout changed distance or predecessor");
	}
}
}

int main()
{
	try
	{
		stableTiesAndWeightedRelaxation();
		footprintAndBypassCosts();
		invalidAndFlyingStarts();
		randomizedReferenceParity();
		std::cout << "Battle movement solver: route cases and 1200 reference comparisons passed\n";
		return EXIT_SUCCESS;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
