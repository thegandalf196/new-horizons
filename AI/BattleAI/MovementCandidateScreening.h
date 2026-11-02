/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

#include "../../lib/battle/BattleHexArray.h"
#include <cstdint>

struct ReachabilityInfo;
struct MoveTarget;
class PotentialTargets;

namespace movementCandidateScreening
{
/// Approximate search budget, not a score bound. Caller must first admit the
/// visible ordinary movement state. Preserve original iteration order and the
/// first candidate; retain a shortest route and two further distinct first-turn
/// destinations. Invalid/occupied routes retain the complete candidate set.
BattleHexArray promisingApproaches(const BattleHexArray & hexes,
	const ReachabilityInfo & reachability, uint32_t speed);

/// Existing forced-action movement, independent of tactical screening.
MoveTarget forcedBerserkMove(const PotentialTargets & targets);
}
