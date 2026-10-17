/*
 * RecipientBonusOverlay.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "Bonus.h"
#include <optional>
#include <set>

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
};

DLL_LINKAGE bool isSnapshotHeroSpecialEffect(const Bonus * bonus);
DLL_LINKAGE bool isInPhysicalAfflictionGroup(const Bonus * bonus, const Groups & history);
DLL_LINKAGE bool isCapturedProjectionEffect(const Bonus * bonus, const Groups & history);
DLL_LINKAGE void copyInherited(BonusList & target, const BonusList & inherited,
	const View & layer, bool unstacked);
DLL_LINKAGE void append(BonusList & target, const View & layer,
	const CSelector & mergeSelector, bool unstacked);
DLL_LINKAGE TConstBonusListPtr finish(TBonusListPtr target, const View & layer, const CSelector & selector);
}
