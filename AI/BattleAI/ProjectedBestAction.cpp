/* ProjectedBestAction.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "ProjectedBestAction.h"
#include "ForecastNativeAdapter.h"
#include "HypotheticBattle.h"
#include "AttackPossibility.h"
#include <stdexcept>
void applyProjectedBestAction(HypotheticBattle & state, const battle::Unit * liveUnit,
	const AttackPossibility & action)
{
	if(!liveUnit || !action.attack.attacker || liveUnit->unitId() != action.attack.attacker->unitId())
		throw std::logic_error("Accepted action actor changed before current destination binding");
	const auto * current = state.battleGetUnitByID(liveUnit->unitId());
	if(!current) throw std::logic_error("Accepted action actor is absent from current destination");
	if(!forecast::NativeAdapter::applyAcceptedAttack(state, current, action, forecast::AcceptedAttackRoute::BestAction))
		forecast::NativeAdapter::replayAcceptedAttackNative(state, liveUnit, action, forecast::AcceptedAttackRoute::BestAction);
}
