/*
 * NewHorizonsElementalRebirth.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "AccessibilityInfo.h"
#include "Unit.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"

#include <optional>
#include <vector>

class CArmedInstance;
class CGHeroInstance;

namespace newHorizonsElementalRebirth
{
/// Saved-rank-derived profile. Rank 1/2/3 maps to the canonical 25/40/50 HP percentages.
struct DLL_LINKAGE ActiveProfile
{
	int rank = 0;
	int healthPercent = 0;
};

/// Immutable pre-hit facts needed to decide and resolve one destruction reaction.
struct DLL_LINKAGE DeathSnapshot
{
	uint32_t unitId = 0;
	BattleSide side = BattleSide::NONE;
	BattleHex corpsePosition;
	int64_t battleStartMaximumAggregateHP = 0;
	ActiveProfile profile;
};

/// Exact-health arithmetic for a temporary Elemental stack. `damageFromFull` is applied
/// after the normal ADD so the ordinary unit-state update replicates the top wound.
struct DLL_LINKAGE SpawnHealth
{
	int32_t count = 0;
	int32_t effectiveCreatureMaxHP = 0;
	int64_t targetAggregateHP = 0;
	int64_t fullAggregateHP = 0;
	int64_t damageFromFull = 0;
	int32_t firstCreatureHP = 0;
};

struct DLL_LINKAGE SpawnDescriptor
{
	battle::UnitInfo unit;
	SpawnHealth health;
};

/// Returns the active canonical profile using the hero's saved faction-skill and perk snapshots.
DLL_LINKAGE std::optional<ActiveProfile> activeProfile(const CGHeroInstance * hero);

/// Whether a pre-hit unit can be a source (ordinary, non-summoned, non-clone creature stack).
DLL_LINKAGE bool isEligibleSource(const battle::Unit & unit);

/// Captures the source identity, current corpse hex, frozen battle-start HP basis and active rank.
DLL_LINKAGE std::optional<DeathSnapshot> captureDeathSource(
	const battle::Unit & unit, const CGHeroInstance * hero);

/// Revalidates a captured source after applying the hit. A native Rebirth survivor and clone
/// death are rejected even if the triggering hit packet reported a lethal amount.
DLL_LINKAGE bool stillEligibleDeath(const battle::Unit * postHitUnit, const DeathSnapshot & snapshot,
	bool hitKilled, bool cloneKilled, bool nativeRebirth);

/// Five canonical Conflux Elite Elemental result types that fit at the exact corpse anchor.
DLL_LINKAGE std::vector<CreatureID> legalCandidatePool(
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules,
	const AccessibilityInfo & accessibility, BattleHex corpsePosition, BattleSide side);

DLL_LINKAGE int64_t targetHP(const DeathSnapshot & snapshot);
/// Source-only hypothetical CStack probe; does not attach a live child or invalidate caches.
DLL_LINKAGE int32_t effectiveSummonMaxHP(const CArmedInstance * sourceArmy,
	CreatureID creature, PlayerColor owner, BattleSide side);
DLL_LINKAGE std::optional<SpawnHealth> spawnHealth(int64_t targetAggregateHP, int32_t effectiveCreatureMaxHP);
DLL_LINKAGE std::optional<SpawnDescriptor> makeSpawnDescriptor(uint32_t unitId, CreatureID creature,
	BattleSide side, BattleHex corpsePosition, int64_t targetAggregateHP, int32_t effectiveCreatureMaxHP);
}
