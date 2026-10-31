/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "PotentialTargets.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"

namespace
{
const battle::Unit * originalUnit(const std::vector<battle::Units> & order,
	const HypotheticBattle & source, uint32_t id)
{
	for(const auto & queue : order)
		for(const auto * unit : queue)
			if(unit->unitId() == id) return unit;
	return source.battleGetUnitByID(id);
}
battle::Units adjacentUnits(const battle::Unit * blockerUnit, const CBattleInfoCallback & geometry)
{
	std::queue<const battle::Unit *> queue;
	battle::Units checkedStacks;

	queue.push(blockerUnit);

	while(!queue.empty())
	{
		auto stack = queue.front();

		queue.pop();
		checkedStacks.push_back(stack);

		auto const & hexes = stack->getSurroundingHexes();
		for(const auto & hex : hexes)
		{
			auto neighbour = geometry.battleGetUnitByPos(hex);

			if(neighbour && neighbour->unitSide() == stack->unitSide() && !vstd::contains(checkedStacks, neighbour))
			{
				queue.push(neighbour);
				checkedStacks.push_back(neighbour);
			}
		}
	}

	return checkedStacks;
}


}
battle::Units BattleExchangeEvaluator::getAdjacentUnits(const battle::Unit * unit) const
{
	return adjacentUnits(unit, *cb);
}
std::shared_ptr<HypotheticBattle> BattleExchangeEvaluator::prepareReturnGateReachability(
	const AttackPossibility & ap, const std::shared_ptr<HypotheticBattle> & source, ReachabilityMapCache & cache) const
{
	const auto geometry = ap.completedReturnDestination && ap.effectPreview ? ap.effectPreview : source;
	if(geometry->battleGetGateState() != EGateState::OPENED || cb->battleGetGateState() == EGateState::OPENED)
		return {};
	// Rebuild existing geometry maps once. Keep original turn identities/scalar
	// simulation; only the map's unit-position/gate facts use the retained result.
	auto geometryOrder = turnOrder;
	for(auto & queue : geometryOrder)
		for(auto & unit : queue)
			if(const auto * current = geometry->battleGetUnitByID(unit->unitId())) unit = current;
	cache.update(geometryOrder, geometry);
	return geometry;
}
ReachabilityData BattleExchangeEvaluator::getExchangeUnits(const AttackPossibility & ap, uint8_t turn,
	PotentialTargets & targets, std::shared_ptr<HypotheticBattle> hb, const battle::Units & additionalUnits) const
{
	ReachabilityMapCache local;
	const auto geometry = prepareReturnGateReachability(ap, hb, local);
	return getExchangeUnitsWithGeometry(ap, turn, targets, hb, additionalUnits, geometry ? &local : nullptr, geometry);
}
ReachabilityData BattleExchangeEvaluator::getExchangeUnitsWithGeometry(
	const AttackPossibility & ap,
	uint8_t turn,
	PotentialTargets & targets,
	std::shared_ptr<HypotheticBattle> hb,
	const battle::Units & additionalUnits, ReachabilityMapCache * returnCache,
	const std::shared_ptr<HypotheticBattle> & returnGeometry) const
{
	ReachabilityData result;
	const auto reachable = [&](uint8_t index, BattleHex hex) -> const battle::Units &
	{
		return returnCache ? returnCache->getOneTurnReachableUnits(returnGeometry, env, turnOrder, index, hex)
			: getOneTurnReachableUnits(index, hex);
	};

	auto hexes = ap.attack.defender->getSurroundingHexes();

	if(!ap.attack.shooting)
		hexes.insert(ap.from);

	battle::Units allReachableUnits = additionalUnits;

	for(const auto & hex : hexes)
	{
		vstd::concatenate(allReachableUnits, reachable(turn, hex));
	}

	if(!ap.attack.attacker->isTurret())
	{
		for(const auto & hex : ap.attack.attacker->getHexes())
		{
			auto unitsReachingAttacker = reachable(turn, hex);
			for(auto unit : unitsReachingAttacker)
			{
				if(unit->unitSide() != ap.attack.attacker->unitSide())
				{
					allReachableUnits.push_back(unit);
					result.enemyUnitsReachingAttacker.insert(unit->unitId());
				}
			}
		}
	}

	vstd::removeDuplicates(allReachableUnits);

	auto copy = allReachableUnits;
	for(auto unit : copy)
	{
		for(auto adjacentUnit : returnGeometry
			? adjacentUnits(returnGeometry->battleGetUnitByID(unit->unitId()), *returnGeometry) : getAdjacentUnits(unit))
		{
			auto unitWithBonuses = returnGeometry ? originalUnit(turnOrder, *hb, adjacentUnit->unitId())
				: hb->battleGetUnitByID(adjacentUnit->unitId());

			if((returnGeometry ? std::ranges::any_of(targets.unreachableEnemies, [adjacentUnit](const battle::Unit * target)
				{ return target->unitId() == adjacentUnit->unitId(); }) : vstd::contains(targets.unreachableEnemies, adjacentUnit))
				&& !vstd::contains(allReachableUnits, unitWithBonuses))
			{
				allReachableUnits.push_back(unitWithBonuses);
			}
		}
	}

	vstd::removeDuplicates(allReachableUnits);

	if(!vstd::contains(allReachableUnits, ap.attack.attacker))
	{
		allReachableUnits.push_back(ap.attack.attacker);
	}

	if(allReachableUnits.size() < 2)
	{
#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Reachability map contains only %d stacks", allReachableUnits.size());
#endif

		return result;
	}

	for(auto unit : allReachableUnits)
	{
		auto accessible = !unit->canShoot() || vstd::contains(additionalUnits, unit);

		if(!accessible)
		{
			for(const auto & hex : unit->getSurroundingHexes())
			{
				if(ap.attack.defender->coversPos(hex))
				{
					accessible = true;
				}
			}
		}

		if(accessible)
			result.melleeAccessible.push_back(unit);
		else
			result.shooters.push_back(unit);
	}

	for(int turn = 0; turn < turnOrder.size(); turn++)
	{
		for(auto unit : turnOrder[turn])
		{
			if(vstd::contains(allReachableUnits, unit))
				result.units[turn].push_back(unit);
		}

		vstd::erase_if(result.units[turn], [&](const battle::Unit * u) -> bool
			{
				return !hb->battleGetUnitByID(u->unitId())->alive();
			});
	}

	return result;
}
