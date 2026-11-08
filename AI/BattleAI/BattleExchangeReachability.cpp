/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "HypotheticBattle.h"
#include "EnemyBypassReachability.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include <limits>

void ReachabilityMapCache::update(const std::vector<battle::Units> & turnOrder, std::shared_ptr<HypotheticBattle> hb)
{
	for(auto turn : turnOrder)
	{
		for(auto u : turn)
		{
			if(!vstd::contains(unitReachabilityMap, u->unitId()))
			{
				unitReachabilityMap[u->unitId()] = hb->getReachability(u);
			}
		}
	}

	hexReachabilityPerTurn.clear();
}

void BattleExchangeEvaluator::updateReachabilityMap(std::shared_ptr<HypotheticBattle> hb)
{
	const int TURN_DEPTH = 2;

	turnOrder.clear();

	hb->battleGetTurnOrder(turnOrder, std::numeric_limits<int>::max(), TURN_DEPTH);
	reachabilityMap.update(turnOrder, hb);
	initiativeStrikeEffects = newHorizonsOrdinaryStrike::mayChangeInitiative(*hb);
}

const battle::Units & ReachabilityMapCache::getOneTurnReachableUnits(std::shared_ptr<CBattleInfoCallback> cb, std::shared_ptr<Environment> env, const std::vector<battle::Units> & turnOrder, uint8_t turn, const BattleHex & hex)
{
	auto & turnData = hexReachabilityPerTurn[turn];

	if (!turnData.isValid[hex.toInt()])
	{
		turnData.hexes[hex.toInt()] = computeOneTurnReachableUnits(cb, env, turnOrder, turn, hex);
		turnData.isValid.set(hex.toInt());
	}

	return turnData.hexes[hex.toInt()];
}

battle::Units ReachabilityMapCache::computeOneTurnReachableUnits(std::shared_ptr<CBattleInfoCallback> cb, std::shared_ptr<Environment> env, const std::vector<battle::Units> & turnOrder, uint8_t turn, const BattleHex & hex)
{
	battle::Units result;
	if(turnOrder.empty())
		return result;
	// These queries never advance or mutate the projection. Reuse it only for
	// this hex/turn-order evaluation; no state is retained across cache misses.
	HypotheticBattle turnBattle(env.get(), cb);

	for(int i = 0; i < turnOrder.size(); i++, turn++)
	{
		auto & turnQueue = turnOrder[i];

		for(const battle::Unit * unit : turnQueue)
		{
			if(unit->isTurret())
				continue;

			if(turnBattle.battleCanShoot(unit))
			{
				result.push_back(unit);

				continue;
			}

			auto unitSpeed = unit->getMovementRange(turn);
			auto radius = unitSpeed * (turn + 1);

			auto reachabilityIter = unitReachabilityMap.find(unit->unitId());
			assert(reachabilityIter != unitReachabilityMap.end()); // missing updateReachabilityMap call?

			std::optional<ReachabilityInfo> fallbackReachability;
			const ReachabilityInfo * reachability = nullptr;
			if(reachabilityIter != unitReachabilityMap.end())
				reachability = &reachabilityIter->second;
			else
			{
				fallbackReachability.emplace(turnBattle.getReachability(unit));
				reachability = &*fallbackReachability;
			}
			const ReachabilityInfo & unitReachability = *reachability;

			// Distances may be finite through Passing Lines (or Ghost Walk) even
			// when this occupied hex is not a legal movement destination.
			bool reachable = unitReachability.isReachable(hex)
				&& unitReachability.distances.at(hex.toInt()) <= radius;

			if(!reachable && unitReachability.accessibility[hex.toInt()] == EAccessibility::ALIVE_STACK)
			{
				const battle::Unit * hexStack = turnBattle.battleGetUnitByPos(hex);

				if(hexStack && controlledByDifferentPlayer(turnBattle, unit, hexStack))
				{
					for(const BattleHex & neighbour : hex.getNeighbouringTiles())
					{
						reachable = unitReachability.isReachable(neighbour)
							&& unitReachability.distances.at(neighbour.toInt()) <= radius;

						if(reachable) break;
					}
				}
			}

			if(reachable)
			{
				result.push_back(unit);
			}
		}
	}

	return result;
}

const battle::Units & BattleExchangeEvaluator::getOneTurnReachableUnits(uint8_t turn, const BattleHex & hex) const
{
	return reachabilityMap.getOneTurnReachableUnits(cb, env, turnOrder, turn, hex);
}


bool ReachabilityMapCache::canReachByTurn(const battle::Unit & actor, const battle::Unit & target,
	int turn, bool shooting, const CBattleInfoCallback & state) const
{
	if(!actor.alive() || !target.alive() || actor.isGhost() || target.isGhost()
		|| state.battleGetOwner(&actor) == state.battleGetOwner(&target)) return false;
	if(shooting) return state.battleCanShootAction(&actor, target.getPosition());
	if(!state.battleCanAttackUnitAction(&actor, &target)) return false;
	if(state.isMeleeAttackPossibleWithLongReach(&actor, &target, actor.getPosition())) return true;
	const auto cached = unitReachabilityMap.find(actor.unitId());
	if(cached == unitReachabilityMap.end())
		throw std::logic_error("No-action target admission needs its prepared unit reachability");
	// The existing exchange keeps positions static and expands the movement
	// allowance by round. Reuse that bounded approximation without a new path.
	const int64_t radius = static_cast<int64_t>(actor.getMovementRange()) * (turn + 1);
	for(const auto & cell : target.getHexes())
		for(const auto & neighbor : cell.getNeighbouringTiles())
			if(cached->second.isReachable(neighbor)
				&& cached->second.distances.at(neighbor.toInt()) <= radius) return true;
	return false;
}
