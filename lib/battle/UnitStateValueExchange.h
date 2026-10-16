/*
 * UnitStateValueExchange.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

namespace battle
{
class CUnitState;

/// Allocation-free exchange of prepared values; NOT an UPDATE transaction.
/// Preserves each destination's descriptor, environment, caches and health/ammo
/// owners. Does not invoke form hooks, invalidate bonuses, or change form view
/// revisions: a full prepared world/graph owner must commit those separately.
/// Both arguments must outlive the enclosing plan and contain validated state.
DLL_LINKAGE void exchangePreparedUnitStateValues(CUnitState & destination, CUnitState & prepared) noexcept;
}
