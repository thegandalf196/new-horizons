/*
 * SkeletonTransformer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../constants/EntityIdentifiers.h"

#include <cstdint>
#include <span>
#include <vector>

class CArmedInstance;
class JsonNode;

namespace newHorizonsSkeletonTransformer
{
enum class PlanStatus : uint8_t
{
	LEGACY_RULES,
	READY,
	EMPTY_SELECTION,
	INVALID_SLOT,
	DUPLICATE_SLOT,
	INVALID_COUNT,
	HP_OVERFLOW,
	ZERO_OUTPUT,
	LAST_STACK,
	LEADERSHIP_LIMIT,
	OUTPUT_CAPACITY
};

struct DLL_LINKAGE OutputStack
{
	SlotID slot;
	/// Final Skeleton count at this destination, including retained Skeletons.
	int64_t count = 0;
};

struct DLL_LINKAGE ProjectedStack
{
	SlotID slot;
	CreatureID creature;
	int64_t count = 0;
};

/// Pure projection of one atomic Transformer selection. It never mutates army
/// state; the authoritative caller must validate market/ownership and apply a
/// READY plan through normal server state changes.
struct DLL_LINKAGE Plan
{
	PlanStatus status = PlanStatus::EMPTY_SELECTION;
	int64_t sacrificedHitPoints = 0;
	int64_t convertedHitPoints = 0;
	int64_t skeletonCount = 0;
	std::vector<OutputStack> outputs;
	/// Complete occupied army: projected on READY, original on rejection.
	std::vector<ProjectedStack> projectedArmy;

	bool isReady() const { return status == PlanStatus::READY; }
};

/// Reads the explicit saved-world capability marker; absent markers retain the
/// original per-slot type-substitution behavior for historical saves.
DLL_LINKAGE Plan plan(const CArmedInstance & army, std::span<const SlotID> selectedSlots,
	const JsonNode & savedCapabilityRules);
}
