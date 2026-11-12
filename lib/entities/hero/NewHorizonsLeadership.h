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
#include "NewHorizonsCommandArmy.h"
#include "../../constants/EntityIdentifiers.h"
#include <cstdint>
#include <optional>
#include <span>

class CArmedInstance;
class CCreatureSet;
class CGHeroInstance;

namespace newHorizonsHeroes
{
struct LeadershipSlotCapacity;
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

/// Hero-owned captured capacity plus trained Leadership and applicable creature
/// policy. Shared admission callers continue to use the hero's public method.
DLL_LINKAGE std::optional<LeadershipSlotCapacity> heroLeadershipSlotCapacity(
	const CGHeroInstance & hero, CreatureID creature);

/// Computes effective Hero Leadership for commanding a specific creature stack,
/// applying Authority rank and applicable Authority perks according to the formula:
/// L_effective = floor(L_hero * (1 + A + P_B + P_A + P_E)).
DLL_LINKAGE int64_t stackEffectiveLeadership(const CGHeroInstance & hero, CreatureID creature,
	const CCreatureSet * armyContext = nullptr);

/// Computes effective Creature Leadership Requirement, applying Necromancy rank
/// and Lord of the Dead discounts for Skeletons/Skeleton Warriors:
/// R_effective = max(1, ceil(R_base * (1 - d))).
DLL_LINKAGE int64_t stackEffectiveRequirement(const CGHeroInstance * hero, CreatureID creature);

/// Computes full-efficiency capacity from numerical ratings:
/// C = max(1, floor(L_effective / R_effective)).
DLL_LINKAGE int64_t computeFullEfficiencyCapacity(int64_t effectiveLeadership, int64_t effectiveRequirement);

/// Computes full-efficiency capacity for a creature stack:
/// C = max(1, floor(L_effective / R_effective)).
DLL_LINKAGE int64_t stackFullEfficiencyCapacity(const CGHeroInstance * hero, CreatureID creature,
	const CCreatureSet * armyContext = nullptr);

/// Square-root Leadership Efficiency for a stack of count creatures against capacity.
/// When count <= capacity, returns 1.0.
/// When count > capacity, returns (capacity + (count - capacity) * sqrt(capacity / count)) / count.
DLL_LINKAGE double squareRootLeadershipEfficiency(int64_t count, int64_t capacity);

/// Computes stack Leadership Efficiency for an army stack in [0.0, 1.0].
/// Hero-less armies and neutral armies return 1.0.
DLL_LINKAGE double stackLeadershipEfficiency(const CGHeroInstance * hero, CreatureID creature,
	int64_t count, const CCreatureSet * armyContext = nullptr);

/// Read-only occupied slot for recruitment forecasting; counts stay wide across
/// repeated purchases. This is transient input, not saved army state.

/// Shared matching/empty/best-headroom resolver. Slots must be valid and unique;
/// row order is immaterial. An absent maximum preserves getSlotFor ordering.
DLL_LINKAGE SlotID recruitmentSlot(std::span<const RecruitmentSlot> slots,
	CreatureID creature, int64_t requestedAmount, std::optional<int64_t> maximum);

/// Select the single recruitment destination for one request without splitting
/// that request across stacks. Uncapped/legacy armies preserve getSlotFor.
DLL_LINKAGE SlotID recruitmentSlot(const CArmedInstance * army, CreatureID creature,
	int64_t requestedAmount = 1);

/// Predict full atomic army admission with moveArmy's matching, empty-slot and
/// duplicate-consolidation order. No splitting, partial transfer or mutation.
/// Shared by client preflight and AI; authoritative validation remains required.
DLL_LINKAGE bool canMergeArmies(const CArmedInstance * source, const CArmedInstance * destination);
/// The same deterministic merge for projected slots, with the real destination hero's saved capacity policy.
DLL_LINKAGE bool canMergeArmies(const CCreatureSet * source, const CCreatureSet * destination,
	const CGHeroInstance * destinationHero);
}
