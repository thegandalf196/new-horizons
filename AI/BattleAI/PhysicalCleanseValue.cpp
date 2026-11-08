/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PhysicalCleanseValue.h"
#include "CreatureActivationValue.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "AttackPossibility.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include <stdexcept>
namespace physicalCleanseAI
{
float MobilityQuery::crippleValue(const CBattleInfoCallback & callback, const battle::Unit & unit)
{
	if(source && source != &callback) throw std::invalid_argument("Mobility memo cannot cross selector worlds");
	source = &callback;
	if(const auto found = values.find(&unit); found != values.end()) return found->second;
	const auto * board = dynamic_cast<const HypotheticBattle *>(&callback);
	if(!board || board->battleGetUnitByID(unit.unitId()) != &unit) return 0;
	const auto removal = newHorizonsAttackControllers::crippleRemoval(unit);
	if(removal.size() != 2 || !std::ranges::any_of(removal, [](const auto & bonus)
		{ return bonus.type == BonusType::STACKS_MOVEMENT_RANGE && bonus.turnsRemain > 0; })) return 0;
	// Established scratch-unit construction preserves exact source/provider
	// ancestry. Only its local overlay changes; the owning HB is never written.
	StackWithBonuses restored(board, &unit);
	restored.removeUnitBonus(removal);
	const int beforeRadius = unit.getMovementRange(), afterRadius = restored.getMovementRange();
	if(afterRadius <= beforeRadius) return values.emplace(&unit, 0).first->second;
	// Reachability parameters contain geometry, family, side and movement mode,
	// not Speed. This one complete BFS therefore covers both unchanged footprints.
	const auto distances = board->getReachability(&unit);
	const std::shared_ptr<CBattleInfoCallback> borrowed(const_cast<HypotheticBattle *>(board), [](auto *) {});
	DamageCache beforeCache, afterCache;
	const auto before = creatureActivationValue::expectedTargetActivationValue(&unit, beforeCache,
		borrowed, true, &distances, beforeRadius);
	const auto after = creatureActivationValue::expectedTargetActivationValue(&restored, afterCache,
		borrowed, true, &distances, afterRadius);
	return values.emplace(&unit, std::max(0.0f, after - before)).first->second;
}
}
