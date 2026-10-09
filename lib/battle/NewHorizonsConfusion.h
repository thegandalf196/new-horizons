/*
 * NewHorizonsConfusion.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "CBattleInfoCallback.h"

#include <cstdint>
#include <vector>

namespace newHorizonsConfusion
{
struct DLL_LINKAGE AttackChoice : ForcedAction
{
	/// A moved shot requires the ordinary explicit Skirmisher action flag;
	/// consumers must not translate it into a stationary SHOOT request.
	bool skirmisher = false;
};

struct DLL_LINKAGE AttackTargetChoices
{
	uint32_t targetId = 0;
	/// All ordinary legal attacks against this one enemy. A legal shot does
	/// not suppress legal melee alternatives, nor force Berserk's melee policy.
	std::vector<AttackChoice> attacks;
	/// Existing ordinary approach geometry yields at most one advance endpoint.
	/// This remains separate from attacks: advancing resolves the Attack family.
	std::vector<BattleHex> furthestAdvances;
	/// No attack and no legal nonstationary advance. The shared resolver maps
	/// this selected Attack enemy to the approved ordinary Defend fallback.
	bool zeroAdvanceDefends = false;
};

struct DLL_LINKAGE Choices
{
	/// Sample an enemy group uniformly, then its attack positions; flattening
	/// these groups would incorrectly favour enemies with more legal positions.
	std::vector<AttackTargetChoices> attacks;
	std::vector<BattleHex> wanderDestinations;
	bool wanderFallsBackToDefend = true;
};

/// Read-only geometry shared by authoritative selection and detached forecasts.
/// Does not choose a behavior, consume RNG, change ownership, or mutate a unit.
/// Returned ForcedAction target pointers belong to the supplied battle context.
DLL_LINKAGE Choices enumerateChoices(const CBattleInfoCallback & battle, const battle::Unit * unit);
}
