/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "MovementQueryCache.h"
#include <type_traits>
#include <string_view>
#include <utility>

std::size_t MovementQueryCache::Hash::operator()(const battle::MovementQuery & query) const
{
	std::size_t result = 0;
	const auto combine = [&result](const auto & value)
	{
		using Value = std::decay_t<decltype(value)>;
		result ^= std::hash<Value>{}(value) + 0x9e3779b9u + (result << 6) + (result >> 2);
	};
	combine(query.startPosition);
	combine(query.footprint);
	combine(query.flying);
	combine(query.traversable);
	if(!query.flying)
	{
		combine(query.stoppers);
		// Hash the arrays as bytes, avoiding a serial dependency for every hex.
		// Exact field equality still rejects collisions; never hash struct padding.
		static_assert(std::has_unique_object_representations_v<std::int32_t>);
		static_assert(std::has_unique_object_representations_v<std::int8_t>);
		combine(std::string_view(reinterpret_cast<const char *>(query.movementCost.data()),
			query.movementCost.size() * sizeof(query.movementCost[0])));
		combine(std::string_view(reinterpret_cast<const char *>(query.bypassCost.data()),
			query.bypassCost.size() * sizeof(query.bypassCost[0])));
	}
	return result;
}

MovementQueryCache::MovementQueryCache(std::size_t capacity, Solver solver)
	: capacity(capacity), solver(std::move(solver))
{
}

battle::MovementPaths MovementQueryCache::get(const battle::MovementQuery & query)
{
	std::lock_guard lock(mutex);
	return getLocked(query);
}

battle::MovementPaths MovementQueryCache::getWalking(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers,
	EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts)
{
	std::lock_guard lock(mutex);
	// Admission affects work only, never candidate selection or traversal. Avoid
	// repeated key construction after a decision proves to have no reuse, and
	// preserve the native solver for cheap/small maps. These are performance
	// heuristics; an underestimated reachable region still computes exact paths.
	const bool unproductive = counts.hits == 0 && counts.solves - counts.bypassed >= 32;
	bool bypass = capacity == 0 || params.flying || walkingAdmissionClosed || unproductive;
	bool cheap = false;
	if(!bypass)
	{
		// Most fields are large; stop as soon as the cheap-field predicate fails.
		std::size_t open = 0;
		for(const auto tile : accessibility)
			if(tile == EAccessibility::ACCESSIBLE || tile == EAccessibility::GATE || tile == EAccessibility::ALIVE_STACK)
				if(++open > 24) break;
		cheap = open <= 24;
		bypass = cheap;
	}
	if(!bypass)
		return getLocked(battle::compileWalkingMovementQuery(accessibility, params,
			stoppers, gateState, movementCosts), true);
	++counts.queries;
	auto result = battle::solveMovementDirect(accessibility, params, stoppers, gateState, movementCosts);
	++counts.solves;
	++counts.bypassed;
	// Commit negative admission only after a successful solve. Keep an already
	// populated memo eligible if a single cheap query appears between candidates.
	if(unproductive || (cheap && entries.empty())) walkingAdmissionClosed = true;
	return result;
}

battle::MovementPaths MovementQueryCache::getLocked(const battle::MovementQuery & query, bool walkingAdmission)
{
	++counts.queries;
	if(const auto found = entries.find(query); found != entries.end())
	{
		++counts.hits;
		return found->second;
	}
	const bool trackedStart = walkingAdmission && query.startPosition >= 0
		&& query.startPosition < GameConstants::BFIELD_SIZE && query.footprint < 3;
	const auto startIndex = trackedStart
		? static_cast<std::size_t>(query.startPosition) * 3 + query.footprint : 0;
	// If even the first repeated starting footprint has different exact inputs,
	// the decision has shown no reuse. Decline further key construction/storage;
	// preserve the 32-miss ceiling for queries with all different starts instead.
	const bool closeAdmission = trackedStart && counts.hits == 0 && seenWalkingStarts.test(startIndex);
	// Failed solves publish nothing, cannot evict, and do not affect admission.
	auto result = solver(query);
	++counts.solves;
	if(trackedStart) seenWalkingStarts.set(startIndex);
	if(closeAdmission) walkingAdmissionClosed = true;
	if(capacity == 0 || closeAdmission)
		return result;
	const auto inserted = entries.emplace(query, result).first;
	try
	{
		if(!insertionOrder) insertionOrder.emplace();
		insertionOrder->push_back(&inserted->first);
	}
	catch(...)
	{
		entries.erase(inserted);
		throw;
	}
	if(entries.size() > capacity)
	{
		entries.erase(*insertionOrder->front());
		insertionOrder->pop_front();
		++counts.evictions;
	}
	return result;
}

MovementQueryCache::Statistics MovementQueryCache::statistics() const
{
	std::lock_guard lock(mutex);
	auto result = counts;
	result.entries = entries.size();
	return result;
}
