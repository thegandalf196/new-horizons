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
#include "../bonuses/Bonus.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

class CArmedInstance;
class CBattleInfoCallback;
class CGHeroInstance;
class IBattleInfo;

namespace newHorizonsElementalRebirth
{
/// Saved-rank/perk-derived profile. Rank 1/2/3 maps to the canonical 25/40/50 HP percentages;
/// selected advanced perks are captured here so death resolution does not re-read mutable hero state.
struct DLL_LINKAGE ActiveProfile
{
	int rank = 0;
	int healthPercent = 0;
	bool primalBurst = false;
	bool greaterEssence = false;
	bool elementalWard = false;
	bool rebirthChain = false;
	bool elementalAttunement = false;
	bool adaptiveElement = false;
	bool perfectConvergence = false;
	bool swiftRebirth = false;
	bool elementalMemory = false;
	bool phoenixSpark = false;
};

/// Immutable pre-hit facts needed to decide and resolve one destruction reaction.
struct DLL_LINKAGE DeathSnapshot
{
	uint32_t unitId = 0;
	BattleSide side = BattleSide::NONE;
	BattleHex corpsePosition;
	int64_t battleStartMaximumAggregateHP = 0;
	ActiveProfile profile;
	bool chain = false;
	int64_t rebirthOriginalAggregateHP = 0;
	int32_t positiveMoraleModifier = 0;
	int32_t positiveLuckModifier = 0;
	bool phoenixSpark = false;
};

/// First eligible Champion death is spent before placement, including a blocked footprint.
struct DLL_LINKAGE PhoenixSparkConsumption
{
	BattleSide side = BattleSide::NONE;
	uint32_t sourceUnitId = std::numeric_limits<uint32_t>::max();
	void validateShape() const;
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateShape();
		h & side;
		h & sourceUnitId;
		if(!h.saving)
			validateShape();
	}
};
DLL_LINKAGE void validatePhoenixSparkConsumption(const IBattleInfo & battle,
	const PhoenixSparkConsumption & consumption);

/// Atomic provenance for one second-generation ADD and its side-owned combat token.
struct DLL_LINKAGE ChainConsumption
{
	BattleSide side = BattleSide::NONE;
	uint32_t sourceUnitId = std::numeric_limits<uint32_t>::max();
	uint32_t spawnUnitId = std::numeric_limits<uint32_t>::max();
	bool rollback = false;

	void validateShape() const;
	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateShape();
		h & side;
		h & sourceUnitId;
		h & spawnUnitId;
		h & rollback;
		if(!h.saving)
			validateShape();
	}
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

/// Total Primal Burst budget is floor(10% of reborn aggregate HP).
DLL_LINKAGE int64_t primalBurstDamageBudget(int64_t rebornAggregateHP);
/// Equal integer share for each unique adjacent hostile; any remainder is discarded.
DLL_LINKAGE int64_t primalBurstShare(int64_t damageBudget, size_t hostileCount);
/// Unique, sorted IDs for living valid targets adjacent to the complete reborn footprint and
/// hostile under current battle ownership/control.
DLL_LINKAGE std::vector<uint32_t> adjacentHostileUnitIds(
	const CBattleInfoCallback & battle, const battle::Unit & center);

/// Bonus applied to each reborn stack when Elemental Ward was active at source death.
DLL_LINKAGE std::optional<Bonus> elementalWardBonus(const ActiveProfile & profile);

/// Round-local positive modifier increment, after the spawned unit's normal inherited
/// bonuses are attached. This copies modifiers without changing Morale/Luck immunity.
DLL_LINKAGE std::vector<Bonus> elementalMemoryBonuses(const DeathSnapshot & snapshot, const battle::Unit & reborn);
/// Exact provenance used by detached round snapshots; other skill stats stay inherited.
DLL_LINKAGE bool isElementalMemoryBonus(const Bonus & bonus);

/// Whether a pre-hit unit can be a source (ordinary, non-summoned, non-clone creature stack).
DLL_LINKAGE bool isEligibleSource(const battle::Unit & unit);

/// Captures the source identity, current corpse hex, frozen battle-start HP basis and active rank.
DLL_LINKAGE std::optional<DeathSnapshot> captureDeathSource(
	const battle::Unit & unit, const CGHeroInstance * hero, bool chainUsed = false,
	const newHorizonsCreatures::CreatureCategoryRules * categoryRules = nullptr, bool phoenixSparkUsed = false);

/// Revalidates a captured source after applying the hit. A native Rebirth survivor and clone
/// death are rejected even if the triggering hit packet reported a lethal amount.
DLL_LINKAGE bool stillEligibleDeath(const battle::Unit * postHitUnit, const DeathSnapshot & snapshot,
	bool hitKilled, bool cloneKilled, bool nativeRebirth, bool chainUsed = false);

/// Validates source lineage, available side quota and exact second-output ADD before mutation.
DLL_LINKAGE void validateChainConsumption(const IBattleInfo & battle, const ChainConsumption & consumption,
	const battle::UnitInfo & spawn);
DLL_LINKAGE void validateChainRollback(const IBattleInfo & battle, const ChainConsumption & consumption);

/// Five canonical Conflux Elite Elemental result types that fit at the exact corpse anchor.
DLL_LINKAGE std::vector<CreatureID> legalCandidatePool(
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules,
	const AccessibilityInfo & accessibility, BattleHex corpsePosition, BattleSide side);

/// Applies saved terrain-selection perks to the ordinary legal candidate pool.
DLL_LINKAGE std::vector<CreatureID> legalCandidatePool(const IBattleInfo & battle,
	const AccessibilityInfo & accessibility, const DeathSnapshot & snapshot);

DLL_LINKAGE int64_t targetHP(const DeathSnapshot & snapshot);
/// Applies matching Elemental Attunement once to the exact first- or second-generation pool.
DLL_LINKAGE int64_t targetHP(const DeathSnapshot & snapshot, CreatureID creature, const IBattleInfo & battle);
/// Source-only hypothetical CStack probe; does not attach a live child or invalidate caches.
DLL_LINKAGE int32_t effectiveSummonMaxHP(const CArmedInstance * sourceArmy,
	CreatureID creature, PlayerColor owner, BattleSide side);
DLL_LINKAGE std::optional<SpawnHealth> spawnHealth(int64_t targetAggregateHP, int32_t effectiveCreatureMaxHP);
DLL_LINKAGE std::optional<SpawnDescriptor> makeSpawnDescriptor(uint32_t unitId, CreatureID creature,
	BattleSide side, BattleHex corpsePosition, int64_t targetAggregateHP, int32_t effectiveCreatureMaxHP,
	bool chainOutput = false);
}
