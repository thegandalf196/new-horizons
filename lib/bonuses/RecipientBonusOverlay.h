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
		&& noexcept(std::swap(veteranCohesionEarned, other.veteranCohesionEarned)))
	{
		added.swap(other.added);
		updated.swap(other.updated);
		removed.swap(other.removed);
		stackedSnapshot.swap(other.stackedSnapshot);
		unstackedSnapshot.swap(other.unstackedSnapshot);
		capturedGroups.swap(other.capturedGroups);
		std::swap(veteranCohesionEarned, other.veteranCohesionEarned);
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
