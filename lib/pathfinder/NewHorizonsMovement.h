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

namespace newHorizonsMovement
{
constexpr int BASE_DAILY_MOVEMENT = 200;
constexpr int ORTHOGONAL_STEP_COST = 10;
constexpr int DIAGONAL_STEP_COST = 14;

/// Applies a percentage-to-base modifier and then a flat modifier to the
/// canonical daily movement pool. The percentage operation is deliberately
/// integer-only so authoritative refresh and pathfinder cannot disagree at
/// boundaries.
DLL_LINKAGE int maximumDailyMovement(std::int64_t percentage, std::int64_t flat = 0);

/// Evaluates the BonusList-compatible movement stages: base value, percentage
/// to base, additive/flat value, then percentage to all.  The two-argument
/// overload above is the canonical shorthand for a 200-point base with no
/// percentage-to-all stage.
DLL_LINKAGE int maximumDailyMovement(std::int64_t baseValue,
	std::int64_t percentageToBase, std::int64_t percentageToAll, std::int64_t additive);

/// Returns the final cost of one adventure-map step. Terrain, road, and
/// special-travel multipliers are represented as exact rational constants and
/// rounded up only after all modifiers have been applied.
DLL_LINKAGE int stepCost(bool diagonal, bool terrainAffinity, bool desert, bool road, bool specialTravel = false);
}
