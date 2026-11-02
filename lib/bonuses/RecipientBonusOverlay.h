/*
 * RecipientBonusOverlay.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "Bonus.h"
#include <optional>
#include <set>
#include <utility>

namespace battle { class Unit; }

namespace recipientBonusOverlay
{
struct ParentClockRecipe;

using Records = std::vector<std::shared_ptr<Bonus>>;
using Values = std::vector<Bonus>;
using Groups = std::set<std::pair<BonusSource, BonusSourceID>>;

/// Nonowning input to the existing recipient-local merge policy. No export
/// owner, propagation operation or limiter applicability is inferred here.
struct View
{
	const Records & added;
	const Records & updated;
	const std::set<std::shared_ptr<Bonus>> & removed;
	const std::optional<Values> & stackedSnapshot;
	const std::optional<Records> & unstackedSnapshot;
	const Groups & capturedGroups;
	const bool & veteranCohesionEarned;
	std::shared_ptr<const ParentClockRecipe> clockParents = nullptr;
};

/// Owned layer values; original record identities/configuration stay borrowed
/// under the real immutable source interval. A value copy is NOT a source lease.
struct DLL_LINKAGE Layer
{
	Records added;
	Records updated;
	std::set<std::shared_ptr<Bonus>> removed;
	std::optional<Values> stackedSnapshot;
	std::optional<Records> unstackedSnapshot;
	Groups capturedGroups;
	bool veteranCohesionEarned = false;
	std::shared_ptr<const ParentClockRecipe> clockParents = nullptr;
	explicit Layer(const View & source);
	View view() const;
	bool hasEdits() const;
	/// Publish prepared values without constructing/moving a set (MSVC may
	/// allocate its sentinel on move construction). Exchange existing storage.
	void swap(Layer & other) noexcept(
		noexcept(added.swap(other.added)) && noexcept(updated.swap(other.updated))
		&& noexcept(removed.swap(other.removed)) && noexcept(stackedSnapshot.swap(other.stackedSnapshot))
		&& noexcept(unstackedSnapshot.swap(other.unstackedSnapshot))
		&& noexcept(capturedGroups.swap(other.capturedGroups))
		&& noexcept(std::swap(veteranCohesionEarned, other.veteranCohesionEarned))
		&& noexcept(clockParents.swap(other.clockParents)))
	{
		added.swap(other.added);
		updated.swap(other.updated);
		removed.swap(other.removed);
		stackedSnapshot.swap(other.stackedSnapshot);
		unstackedSnapshot.swap(other.unstackedSnapshot);
		capturedGroups.swap(other.capturedGroups);
		std::swap(veteranCohesionEarned, other.veteranCohesionEarned);
		clockParents.swap(other.clockParents);
	}
};

/// ADL selects the same storage exchange for generic nothrow publication.
DLL_LINKAGE void swap(Layer & left, Layer & right) noexcept(noexcept(left.swap(right)));

DLL_LINKAGE bool isSnapshotHeroSpecialEffect(const Bonus * bonus);
DLL_LINKAGE bool isInPhysicalAfflictionGroup(const Bonus * bonus, const Groups & history);
DLL_LINKAGE bool isCapturedProjectionEffect(const Bonus * bonus, const Groups & history);
DLL_LINKAGE void copyInherited(BonusList & target, const BonusList & inherited,
	const View & layer, bool unstacked);
DLL_LINKAGE void append(BonusList & target, const View & layer,
	const CSelector & mergeSelector, bool unstacked);
DLL_LINKAGE TConstBonusListPtr finish(TBonusListPtr target, const View & layer, const CSelector & selector);
}

namespace recipientBonusOverlay
{
/// Ordinary recipient delta values only: no native callback, source lease,
/// originating-export claim, current/freshness authority or hero policy.
struct DLL_LINKAGE ParentClockRecipe
{
	std::shared_ptr<const ParentClockRecipe> parent;
	Layer clocks;
	Records orderWitness; // Query-only pointer order, not originating exports or copied bonus values.
	bool hasAliases = false; // Query-record copies, never source freshness/action authority.
	std::vector<std::pair<std::shared_ptr<Bonus>, std::shared_ptr<Bonus>>> aliases;
	explicit ParentClockRecipe(Layer clocks) : clocks(std::move(clocks)) {}
};
/// Collection membership only: UNKNOWN records remain untouched for the canonical native query/error.
DLL_LINKAGE bool isCapturedClockRecord(const Bonus * record, const battle::Unit & subject);
DLL_LINKAGE std::shared_ptr<ParentClockRecipe> captureParentClocks(const View & parent, const battle::Unit & subject);
DLL_LINKAGE void replaceInheritedClocks(BonusList & inherited, const BonusList & terminal,
	const ParentClockRecipe & recipe, const battle::Unit & subject, const CSelector & selector, bool unstacked);
}


namespace recipientBonusOverlay { DLL_LINKAGE std::shared_ptr<Bonus> capturedClockAlias(const ParentClockRecipe & recipe, const std::shared_ptr<Bonus> & record); }
