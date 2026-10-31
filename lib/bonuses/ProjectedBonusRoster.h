/*
 * ProjectedBonusRoster.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../battle/Unit.h"
#include <vector>
#include <span>

/// Borrowed, ordered complete battle roster for a single sequential projection.
/// Unit identities/views must remain alive and ID-stable under the caller's
/// world lease. Values may advance between queries; this is NOT a frozen copy.
/// Membership completeness and graph collection remain the world owner's duty.
namespace battle { class OwnedUnitEnvironment; }
class DLL_LINKAGE ProjectedBonusRoster final
{
	friend class ProjectedBonusGraph;
	friend class battle::OwnedUnitEnvironment;
	std::vector<const battle::Unit *> adjacentUnitsAt(const battle::Unit & unit) const;
	std::vector<const battle::Unit *> units;
	bool orderedOccurrences = false;
	void appendOccurrence(const battle::Unit & unit);
public:
	/// Opt-in historical identity: distinct physical nodes may share packet IDs.
	enum class Identity { UniqueIds, OrderedOccurrences };
	explicit ProjectedBonusRoster(std::vector<const battle::Unit *> units);
	ProjectedBonusRoster(std::vector<const battle::Unit *> units, Identity identity);
	bool usesOrderedOccurrences() const noexcept;
	/// No alive/ghost filtering: preserve first-match packet lookup order.
	const battle::Unit * firstById(uint32_t id) const;
	bool contains(const battle::Unit & unit) const;
	std::span<const battle::Unit * const> orderedUnits() const noexcept;
	std::vector<const battle::Unit *> adjacentUnits(const battle::Unit & unit) const;
};
