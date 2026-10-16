/*
 * ProjectedBonusRoster.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../battle/Unit.h"
#include <vector>

/// Borrowed, ordered complete battle roster for a single sequential projection.
/// Unit identities/views must remain alive and ID-stable under the caller's
/// world lease. Values may advance between queries; this is NOT a frozen copy.
/// Membership completeness and graph collection remain the world owner's duty.
class DLL_LINKAGE ProjectedBonusRoster final
{
	std::vector<const battle::Unit *> units;
public:
	explicit ProjectedBonusRoster(std::vector<const battle::Unit *> units);
	bool contains(const battle::Unit & unit) const;
	std::vector<const battle::Unit *> adjacentUnits(const battle::Unit & unit) const;
};
