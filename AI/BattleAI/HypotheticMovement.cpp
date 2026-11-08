/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "MovementQueryCache.h"

battle::MovementPaths HypotheticBattle::movementPaths(const AccessibilityInfo & accessibility,
	const ReachabilityInfo::Parameters & params, const BattleHexArray & stoppers,
	EGateState gateState, const std::array<std::int32_t, GameConstants::BFIELD_SIZE> & movementCosts) const
{
	return movementMemo ? movementMemo->getWalking(accessibility, params, stoppers, gateState, movementCosts)
		: battle::solveMovementDirect(accessibility, params, stoppers, gateState, movementCosts);
}
