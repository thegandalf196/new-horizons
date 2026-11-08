/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include "MovementBenchmarkReference.h"
#include "../../test/battle/MovementLegacyReference.h"

// Keep the frozen reference independent and expose its call-boundary cost as
// an additional control. The existing inline legacy variant remains reported.
battle::MovementPaths movementBenchmark::legacyExternal(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers,
	EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts)
{
	return movementLegacy::solve(accessibility, params, stoppers, gateState, movementCosts);
}
