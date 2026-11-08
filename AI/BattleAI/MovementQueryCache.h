/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/battle/BattleMovementQuery.h"
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

/// Decision-owned memo of pure movement computations. No units, callbacks,
/// observers, Parameters pointers or game state are retained. Exact key equality
/// resolves hash collisions; all current endpoint metadata belongs to the caller.
class MovementQueryCache
{
public:
	using Solver = std::function<battle::MovementPaths(const battle::MovementQuery &)>;
	struct Statistics
	{
		std::size_t queries = 0;
		std::size_t hits = 0;
		std::size_t solves = 0;
		std::size_t evictions = 0;
		std::size_t entries = 0;
		std::size_t bypassed = 0;
	};
	explicit MovementQueryCache(std::size_t capacity = 256, Solver solver = battle::solveMovementQuery);
	battle::MovementPaths get(const battle::MovementQuery & query);
	battle::MovementPaths getWalking(const AccessibilityInfo & accessibility,
		const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers,
		EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts);
	Statistics statistics() const;

private:
	battle::MovementPaths getLocked(const battle::MovementQuery & query, bool walkingAdmission = false);
	struct Hash
	{
		std::size_t operator()(const battle::MovementQuery & query) const;
	};
	const std::size_t capacity;
	const Solver solver;
	// Current Hero candidates run serially. Locking also makes sharing safe for
	// callers that execute independent branches concurrently in the future.
	mutable std::mutex mutex;
	std::unordered_map<battle::MovementQuery, battle::MovementPaths, Hash> entries;
	// unordered_map keeps element references stable across rehashes. Record
	// those addresses instead of allocating and copying a second owned key.
	// A decision containing only flying/cheap queries never allocates a FIFO.
	std::optional<std::deque<const battle::MovementQuery *>> insertionOrder;
	std::bitset<GameConstants::BFIELD_SIZE * 3> seenWalkingStarts;
	bool walkingAdmissionClosed = false;
	Statistics counts;
};
