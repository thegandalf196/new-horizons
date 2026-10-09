/*
 * NewHorizonsMovement.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../GameConstants.h"

#include <cstdint>
#include <optional>

class CGHeroInstance;

namespace newHorizonsMovement
{
constexpr int BASE_DAILY_MOVEMENT = 200;
constexpr int ORTHOGONAL_STEP_COST = 10;
constexpr int DIAGONAL_STEP_COST = 14;

/// Read-only details for the current New Horizons daily Movement limit.
/// Bounds preserve BonusList semantics: lowerBound comes from INDEPENDENT_MAX,
/// while upperBound comes from INDEPENDENT_MIN.
struct DLL_LINKAGE DailyMovementBreakdown
{
	std::int64_t baseAdjustment = 0;
	std::int64_t percentageToBase = 0;
	std::int64_t percentageToAll = 0;
	std::int64_t flat = 0;
	std::optional<std::int64_t> lowerBound;
	std::optional<std::int64_t> upperBound;
	int limit = 0;
};

/// Builds the current-day land or sea limit breakdown from the hero's active
/// Movement bonuses. Legacy Movement rules do not have this breakdown.
DLL_LINKAGE std::optional<DailyMovementBreakdown> currentDailyMovementBreakdown(
	const CGHeroInstance * hero, bool water);

/// Applies a percentage-to-base modifier and then a flat modifier to the
/// canonical daily movement pool. The percentage operation is deliberately
/// integer-only so authoritative refresh and pathfinder cannot disagree at
/// boundaries.
DLL_LINKAGE int maximumDailyMovement(std::int64_t percentage, std::int64_t flat = 0);

/// Fixed final sea embark/disembark cost: ceil(10% of the source-layer daily budget).
DLL_LINKAGE int rapidEmbarkationCost(int maximumDailyMovement);

/// Positive Pursuit March recovery, capped by missing current-layer daily Movement.
DLL_LINKAGE int pursuitMarchRestoration(int remainingMovement, int maximumDailyMovement);

/// Evaluates the BonusList-compatible movement stages: base value, percentage
/// to base, additive/flat value, then percentage to all.  The two-argument
/// overload above is the canonical shorthand for a 200-point base with no
/// percentage-to-all stage.
DLL_LINKAGE int maximumDailyMovement(std::int64_t baseValue,
	std::int64_t percentageToBase, std::int64_t percentageToAll, std::int64_t additive);

/// Returns the final cost of one adventure-map step. Terrain, road, and
/// special-travel multipliers are represented as exact rational constants and
/// rounded up only after all modifiers have been applied.
DLL_LINKAGE int stepCost(bool diagonal, bool terrainAffinity, bool desert, bool road,
	bool specialTravel = false, bool pathfinding = false, bool roadmaster = false, bool wayfarer = false);
}
