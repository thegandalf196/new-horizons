/*
 * BattleUnitRootOrigin.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../CStack.h"
#include <memory>
#include <optional>
#include <vector>

namespace battle
{
/// Initialized native descriptor/binding facts; not a second unit-value list.
/// In particular, do NOT read CShots::env before native ADD binding: that legacy
/// member is uninitialized. The initialized CUnitState environment is sufficient
/// to detect rebinding; commit overwrites all three environment pointers.
struct BattleUnitRootIdentity
{
	const CStack * object;
	uint32_t id;
	CreatureID nativeType;
	uint32_t baseAmount;
	PlayerColor owner;
	BattleSide side;
	SlotID slot;
	const CStackInstance * base;
	BattleHex initialPosition;
	int64_t battleStartMaximumAggregateHP;
	int64_t rebirthOriginalAggregateHP;
	const BattleInfo * battle;
	const IUnitEnvironment * environment;
	std::optional<TerrainId> queryTerrain;
	bool hypothetical;
	bool operator==(const BattleUnitRootIdentity &) const = default;
};

struct BattleUnitRootOrigin
{
	const std::unique_ptr<CStack> * data;
	size_t size;
	size_t capacity;
	std::vector<BattleUnitRootIdentity> occurrences;
	int32_t generation;
};
}
