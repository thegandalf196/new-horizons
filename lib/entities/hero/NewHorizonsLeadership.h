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
#include "../../constants/EntityIdentifiers.h"
#include <cstdint>

class CArmedInstance;

namespace newHorizonsHeroes
{
/// Capacity arithmetic and the read-only result for a saved-capability hero.
/// The primitive never mutates an army or movement budget.  `movementPercent`
/// is retained as an inspectable over-capacity diagnostic for saved views; it
/// is not a shared New Horizons movement multiplier.
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
/// Above capacity, the result exposes the proportional diagnostic bounded by
/// the declared minimum. It neither refuses acquisition nor removes creatures,
/// and it does not alter the hero's daily movement pool.
DLL_LINKAGE LeadershipCapacity leadershipCapacity(int classBase, int classPerLevel,
	int level, int skillBonusPercent, uint64_t used, int minimumMovementPercent);

/// Legacy arithmetic retained for compatibility with saved-view/unit tests.
/// New Horizons authoritative TurnInfo/pathfinding deliberately does not call
/// this helper: Leadership is per-stack capacity, never a shared movement
/// budget. Changing armies must not award fresh movement points.
DLL_LINKAGE int leadershipMovement(int unscaledMovement, int movementPercent);

/// Select the single recruitment destination for one request without splitting
/// that request across stacks. Uncapped/legacy armies preserve getSlotFor.
DLL_LINKAGE SlotID recruitmentSlot(const CArmedInstance * army, CreatureID creature,
	int64_t requestedAmount = 1);

/// Predict full atomic army admission with moveArmy's matching, empty-slot and
/// duplicate-consolidation order. No splitting, partial transfer or mutation.
/// Shared by client preflight and AI; authoritative validation remains required.
DLL_LINKAGE bool canMergeArmies(const CArmedInstance * source, const CArmedInstance * destination);
}
