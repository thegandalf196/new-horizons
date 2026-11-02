/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "MovementCandidateScreening.h"
#include "BattleExchangeVariant.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include <algorithm>
#include <optional>
#include <vector>

namespace
{
struct Approach
{
	BattleHex hex;
	BattleHex firstTurn;
	uint32_t distance;
	bool retained = false;
};

std::optional<BattleHex> firstTurnDestination(BattleHex hex,
	const ReachabilityInfo & reachability, uint32_t speed)
{
	BattleHex destination = BattleHex::INVALID;
	// Inspect the complete existing predecessor chain: the ordinary chooser's
	// conditional bypass exchanges must remain exhaustive on occupied routes.
	for(size_t steps = 0; steps < GameConstants::BFIELD_SIZE; ++steps)
	{
		if(hex == reachability.params.startPosition)
			return destination.isValid() ? std::optional(destination) : std::nullopt;
		if(!hex.isValid() || !reachability.isReachable(hex))
			return std::nullopt;
		const auto & footprint = battle::Unit::getHexes(hex,
			reachability.params.doubleWide, reachability.params.side);
		if(footprint.size() != (reachability.params.doubleWide ? 2u : 1u)
			|| std::ranges::any_of(footprint, [&](const BattleHex & occupied)
			{
				return reachability.accessibility[occupied.toInt()] != EAccessibility::ACCESSIBLE;
			}))
			return std::nullopt;
		if(!destination.isValid() && reachability.distances[hex.toInt()] <= speed)
			destination = hex;
		const auto previous = reachability.predecessors[hex.toInt()];
		if(!previous.isValid()
			|| reachability.distances[previous.toInt()] >= reachability.distances[hex.toInt()])
			return std::nullopt;
		hex = previous;
	}
	return std::nullopt;
}
}

namespace movementCandidateScreening
{
BattleHexArray promisingApproaches(const BattleHexArray & hexes,
	const ReachabilityInfo & reachability, uint32_t speed)
{
	if(speed == 0 || reachability.params.flying || reachability.params.passThrough
		|| reachability.params.ghostWalk || reachability.params.friendlyTransit.any()
		|| !reachability.params.startPosition.isValid()
		|| (reachability.params.doubleWide && reachability.params.side != BattleSide::ATTACKER
			&& reachability.params.side != BattleSide::DEFENDER))
		return hexes;
	std::vector<Approach> candidates;
	for(const auto hex : hexes)
	{
		if(!reachability.isReachable(hex))
			continue;
		const auto firstTurn = firstTurnDestination(hex, reachability, speed);
		if(!firstTurn)
			return hexes;
		candidates.push_back({hex, *firstTurn, reachability.distances[hex.toInt()]});
	}
	if(candidates.size() <= 4)
		return hexes;

	// A fixed seed prevents empty/all-negative-score accidents. Equal geometric
	// keys keep original ordinal order; no AP preview or cache is borrowed here.
	candidates.front().retained = true;
	std::vector<size_t> ranked;
	for(size_t index = 0; index < candidates.size(); ++index)
		ranked.push_back(index);
	std::stable_sort(ranked.begin(), ranked.end(), [&](size_t left, size_t right)
	{
		return candidates[left].distance < candidates[right].distance;
	});
	candidates[ranked.front()].retained = true;
	BattleHexArray destinations;
	for(const auto & candidate : candidates)
		if(candidate.retained)
			destinations.insert(candidate.firstTurn);
	unsigned additionalDestinations = 0;
	for(const auto index : ranked)
	{
		auto & candidate = candidates[index];
		if(additionalDestinations == 2)
			break;
		if(destinations.contains(candidate.firstTurn))
			continue;
		candidate.retained = true;
		destinations.insert(candidate.firstTurn);
		++additionalDestinations;
	}
	BattleHexArray selected;
	for(const auto & candidate : candidates)
		if(candidate.retained)
			selected.insert(candidate.hex);
	return selected;
}

MoveTarget forcedBerserkMove(const PotentialTargets & targets)
{
	MoveTarget result;
	// A detached forecast retains Berserk's selected path and forced destination.
	for(const auto & forcedAction : targets.forcedBerserkActions)
		if(forcedAction.type == EActionType::WALK && forcedAction.position.isValid())
			result.positions.insert(forcedAction.position);
	if(!result.positions.empty())
	{
		result.score = 0.0f;
		result.turnsToReach = 1;
	}
	return result;
}
}
