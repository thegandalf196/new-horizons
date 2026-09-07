/*
 * NewHorizonsLeadership.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../GameConstants.h"
#include <cstdint>

namespace newHorizonsHeroes
{
/// Capacity arithmetic and the read-only result for a saved-capability hero.
/// The primitive itself never mutates an army or movement budget; the shared
/// TurnInfo integration applies its percentage to movement limits.
struct DLL_LINKAGE LeadershipCapacity
{
	int64_t capacity;
	uint64_t used;
	int movementPercent;

	bool overCapacity() const { return used > static_cast<uint64_t>(capacity); }
};

/// Class base + (level - 1) * class growth, followed by the skill percentage.
/// No primary rating, creature tier or random roll participates. Inputs are
/// bounded to keep every intermediate representable. Used capacity is supplied
/// by authoritative army accounting; this function never changes an army.
/// Above capacity, movement is proportional to capacity/usage, bounded by the
/// declared minimum. It neither refuses acquisition nor removes creatures.
DLL_LINKAGE LeadershipCapacity leadershipCapacity(int classBase, int classPerLevel,
	int level, int skillBonusPercent, uint64_t used, int minimumMovementPercent);

/// Shared final movement scaling for simulation/pathfinding, not an adjustment
/// to already-spent movement. Integration must retain the ordinary daily refresh
/// and embarkation rules; changing armies must not award fresh movement points.
DLL_LINKAGE int leadershipMovement(int unscaledMovement, int movementPercent);
}
