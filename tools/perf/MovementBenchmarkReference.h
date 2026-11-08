/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/battle/BattleMovementQuery.h"

namespace movementBenchmark
{
battle::MovementPaths legacyExternal(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers,
	EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts);
}
