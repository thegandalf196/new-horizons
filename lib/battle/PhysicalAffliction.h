/*
 * PhysicalAffliction.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "Unit.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace physicalAfflictions
{
/// Metadata carried by the zero-value PHYSICAL_AFFLICTION bonus marker.
struct DLL_LINKAGE MarkerMetadata
{
	std::string kind;
	int64_t applicationOrder = 0;
};

/// One eligible physical-affliction source/sid group, or stored physical Poison.
struct DLL_LINKAGE Affliction
{
	std::string kind;
	BonusSource source = BonusSource::OTHER;
	BonusSourceID sourceID;
	int64_t applicationOrder = 0;
	/// Underlying group bonuses. The PHYSICAL_AFFLICTION marker is excluded.
	std::vector<Bonus> effects;
	/// True when this entry is represented by CUnitState's stored physical Poison fields.
	bool storedPoison = false;
};

/// Read and validate marker metadata. Returns empty for a non-marker bonus.
/// Malformed marker payloads throw std::invalid_argument.
DLL_LINKAGE std::optional<MarkerMetadata> markerMetadata(const Bonus & bonus);

/// Enumerate explicit markers, legacy unmarked Poison/Disease groups, and stored physical Poison.
DLL_LINKAGE std::vector<Affliction> enumerate(const battle::Unit & unit);

/// Select Poison, Disease, Bleeding, then the oldest other eligible affliction.
DLL_LINKAGE std::optional<Affliction> first(const battle::Unit & unit);

/// Return only the selected source/sid group, including its marker. Stored Poison has no bonuses.
DLL_LINKAGE std::vector<Bonus> removalPlan(const battle::Unit & unit, const Affliction & affliction);

/// Stamp an ordered SetStackEffect batch atomically. `removed` is applied first, then each incoming
/// vector in the supplied order. The vectors are rewritten only after all marker data validates.
DLL_LINKAGE void stampEffectChanges(const battle::Unit & unit, const std::vector<Bonus> & removed,
	const std::vector<std::vector<Bonus> *> & incomingInApplyOrder);

/// Stamp one add/update payload using the same ordering rules as stampEffectChanges.
DLL_LINKAGE std::vector<Bonus> stampApplicationOrder(const battle::Unit & unit,
	const std::vector<Bonus> & incomingBonuses);
}
