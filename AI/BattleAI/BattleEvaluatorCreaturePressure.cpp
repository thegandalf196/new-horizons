/*
 * BattleEvaluatorCreaturePressure.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleEvaluator.h"

float BattleEvaluator::beneficialCreaturePressure(const std::shared_ptr<HypotheticBattle> & state,
	uint32_t spentCasterId, DamageCache & cache) const
{
	float value = 0;
	cache.buildCreaturePressureCache(state, side);
	for(const auto * unit : state->getUnitsIf([&](const battle::Unit * unit)
	{
		return unit->alive() && unit->unitId() != spentCasterId && !unit->isTimeStopped();
	}))
	{
		// Reuse ordinary spell evaluation's physical-attack valuation, without
		// advancing turns or running a battle queue for each random outcome.
		PotentialTargets targets(unit, cache, state);
		if(targets.possibleAttacks.empty())
			continue;
		const float pressure = static_cast<float>(targets.bestActionValue());
		value += state->battleGetActionController(unit) == playerID ? pressure : -pressure;
	}
	return value;
}
