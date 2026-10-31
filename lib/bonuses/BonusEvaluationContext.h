/*
 * BonusEvaluationContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "BonusEnum.h"
#include "OwnedBonusEvaluationFacts.h"
#include "../constants/EntityIdentifiers.h"
#include "../constants/Enumerations.h"
#include <optional>
#include <cstdint>
#include <vector>

class CBonusSystemNode;
class CStack;
class BattleInfo;
class CGHeroInstance;
class CCreature;
class CStackInstance;
class ProjectedBonusRoster;
struct Bonus;
namespace battle { class Unit; }

/// Stateless native-aura configuration. Affiliation follows original armies,
/// independently of Hypnotize or Puppet Master action control.
struct DLL_LINKAGE AdjacentUnitFilter
{
	enum class Relationship : uint8_t { ANY, SAME_ARMY, OPPOSING_ARMY };
	enum class NativeTrait : uint8_t { ANY, UNDEAD, LIVING };
	Relationship relationship = Relationship::ANY;
	NativeTrait nativeTrait = NativeTrait::ANY;
	bool includeUpgrades = false;
	bool anyCreature = false;
	bool isDefault() const;
	bool isValid() const;
};

/// Explicit updater/limiter input, NOT a projected bonus graph collector.
/// Original typed hero/army/base identities stay live and immutable. Optional
/// battle-unit values are borrowed from the caller's matching sequential view.
/// Both identities/views must outlive evaluation under the world's access lease.
class DLL_LINKAGE BonusEvaluationContext final
{
public:
	/// Engaged optional means explicit native provenance, including null absence.
	struct NativeContext
	{
		const CGHeroInstance * hero;
		const BattleInfo * battle;
		std::optional<TerrainId> queryTerrain = std::nullopt;
	};
private:
	const CBonusSystemNode & original;
	const OwnedBonusEvaluationFacts * owned = nullptr;
	const battle::Unit * projected = nullptr;
	const ProjectedBonusRoster * roster = nullptr;
	std::optional<NativeContext> native;

public:
	/// Implicit adapter preserves existing live calls using a bonus node.
	BonusEvaluationContext(const CBonusSystemNode & node) noexcept;
	/// Pure owned context; enclosing storage owns both shell and copied facts.
	BonusEvaluationContext(const CBonusSystemNode & shell, const OwnedBonusEvaluationFacts & facts);
	/// Query-scoped owned current unit; no native ownership/admission is established.
	BonusEvaluationContext(const CBonusSystemNode & shell, const OwnedBonusEvaluationFacts & facts, const battle::Unit & unit);
	std::optional<int> heroLevel() const;
	std::optional<int> filteredArmySize(CreatureID creature, FactionID faction, int level) const;
	std::optional<bool> matchesCreature(CreatureID family, bool includeUpgrades) const;
	std::optional<bool> matchesCreatureLevel(uint32_t minimum, uint32_t maximum) const;
	std::optional<bool> matchesCreatureAlignment(EAlignment alignment) const;
	FactionID sourceCreatureFaction(CreatureID source) const;
	std::optional<bool> matchesRank(uint8_t minimum, uint8_t maximum) const;
	std::optional<TerrainId> adventureTerrain() const;
	TerrainId resolveTerrain(std::optional<TerrainId> adventure) const;
	bool nativeTerrainMatches(std::optional<TerrainId> adventure) const;
	/// Requires matching unit ID; the enclosing owner binds battle/lifetimes.
	BonusEvaluationContext(const CStack & node, const battle::Unit & unit);
	BonusEvaluationContext(const CStack & node, const battle::Unit & unit, const ProjectedBonusRoster & roster);
	BonusEvaluationContext(const CStack & node, const battle::Unit & unit,
		const ProjectedBonusRoster & roster, NativeContext provenance);
	bool hasNativeContext() const noexcept;
	const BattleInfo * battleContext() const;
	std::optional<TerrainId> battleTerrain() const;
	/// Terrain-only input never establishes battle/updater/adjacency membership.
	std::optional<TerrainId> queryTerrain() const;
	const CBonusSystemNode & originalNode() const noexcept;
	BonusNodeType getNodeType() const;
	PlayerColor getOwner() const;
	std::optional<int> stackCount() const;
	/// Preserve legacy stable base-instance levels; projected effective creature
	/// level applies only when the battle stack has no adventure base instance.
	std::optional<int> stackLevel() const;
	const battle::Unit * battleUnit() const;
	const CStackInstance * stackInstance() const;
	const CCreature * creature() const;
	std::optional<FactionID> faction() const;
	/// Projected adjacency requires complete roster membership; never live-falls-back.
	std::vector<const battle::Unit *> adjacentUnits() const;
	/// Reads only raw unit identity/health/position and native creature traits;
	/// never performs a neighbor's recursive stack bonus query.
	bool matchesAdjacentUnit(const battle::Unit & neighbor, CreatureID family, const AdjacentUnitFilter & filter) const;
	bool hasMatchingAdjacentUnit(CreatureID family, const AdjacentUnitFilter & filter) const;
	/// Projected acceptance requires a CUnitState view (not a generic Unit wrapper).
	bool acceptsBonus(const Bonus & bonus) const;
};
