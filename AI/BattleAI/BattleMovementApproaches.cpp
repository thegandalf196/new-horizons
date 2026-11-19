/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleExchangeVariant.h"
#include "MovementApproachRanking.h"
#include "MovementCandidateScreening.h"
#include "CreatureCandidateScreening.h"
#include "MovementExchangeTiming.h"
#include "SearchBudget.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include <limits>
#include <utility>
using battleSearchTiming::MovementExchangeScope;

struct MovementApproach
{
	AttackPossibility attack;
	const battle::Unit * enemy;
	BattleHex hex;
	uint32_t turns;
	float multiplier;
};

namespace
{
template<typename Callable>
decltype(auto) measureMovementExchange(BattleMovementTiming * timing, Callable && action)
{
	MovementExchangeScope measurement(timing ? &timing->findMoveExchange : nullptr);
	return std::forward<Callable>(action)();
}

uint32_t distToNearestReachableNeighbour(const ReachabilityInfo & reachability,
	const battle::Unit * attacker, const battle::Unit * defender)
{
	auto attackableHexes = defender->getHexes();
	if(attacker->doubleWide())
	{
		if(defender->doubleWide())
			attackableHexes.insert(battle::Unit::getHexes(defender->occupiedHex(), true, defender->unitSide()));
		else
			attackableHexes.insert(battle::Unit::getHexes(defender->getPosition(), true, defender->unitSide()));
	}
	attackableHexes.eraseIf([defender](const BattleHex & hex)
		{
			return hex.getY() != defender->getPosition().getY() || !hex.isAvailable();
		});

	uint32_t result = ReachabilityInfo::INFINITE_DIST;
	for(const auto & targetHex : attackableHexes)
		for(const auto & neighbour : targetHex.getNeighbouringTiles())
			if(reachability.isReachable(neighbour))
				result = std::min(result, reachability.distances.at(neighbour.toInt()));
	return result;
}

bool controlledBySamePlayer(const CBattleInfoCallback & battle,
	const battle::Unit * first, const battle::Unit * second)
{
	const auto firstOwner = battle.battleGetOwner(first);
	const auto secondOwner = battle.battleGetOwner(second);
	return firstOwner != PlayerColor::CANNOT_DETERMINE && firstOwner == secondOwner;
}


std::vector<MovementApproach> collectApproaches(const battle::Unit * activeStack,
	PotentialTargets & targets, DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & hb,
	const CBattleInfoCallback & callback, const ReachabilityInfo & dists, uint32_t speed,
	bool beamMovement, bool screenMovement)
{
	std::vector<MovementApproach> approaches;
	for(const battle::Unit * enemy : targets.unreachableEnemies)
	{
		battleSearch::require(hb->searchBudget().get(), battleSearch::WorkKind::Movement);
		logAi->trace(
			"Checking movement towards %d of %s",
			enemy->getCount(),
			LIBRARY->creatures()->getById(enemy->creatureId())->getJsonKey());

		auto distance = distToNearestReachableNeighbour(dists, activeStack, enemy);

		if(distance >= GameConstants::BFIELD_SIZE)
			continue;

		if(distance <= speed)
			continue;

		float penaltyMultiplier = 1.0f; // Default multiplier, no penalty
		float closestAllyDistance = std::numeric_limits<float>::max();

		for (const battle::Unit* ally : hb->battleAliveUnits())
		{
			battleSearch::require(hb->searchBudget().get(), battleSearch::WorkKind::Movement);
			if (ally == activeStack || !controlledBySamePlayer(*hb, activeStack, ally))
				continue;

			float allyDistance = distToNearestReachableNeighbour(dists, ally, enemy);
			if (allyDistance < closestAllyDistance)
			{
				closestAllyDistance = allyDistance;
			}
		}

		// If an ally is closer to the enemy, compute the penaltyMultiplier
		if (closestAllyDistance < distance)
		{
			penaltyMultiplier = closestAllyDistance / distance; // Ratio of distances
		}

		auto turnsToReach = (distance - 1) / speed + 1;
		const auto hexes = screenMovement
			? movementCandidateScreening::promisingApproaches(enemy->getAttackableHexes(activeStack), dists, speed)
			: enemy->getAttackableHexes(activeStack);
		std::optional<BattleHex> beamHex;
		if(beamMovement)
			for(const auto hex : hexes)
				{
					battleSearch::require(hb->searchBudget().get(), battleSearch::WorkKind::Movement);
					if(dists.isReachable(hex) && (!beamHex
					|| dists.distances[hex.toInt()] < dists.distances[beamHex->toInt()]))
					beamHex = hex;
				}
		auto enemySpeed = enemy->getMovementRange();
		auto speedRatio = speed / static_cast<float>(enemySpeed);
		auto multiplier = (speedRatio > 1 ? 1 : speedRatio) * penaltyMultiplier;

		for(auto & hex : hexes)
		{
			battleSearch::require(hb->searchBudget().get(), battleSearch::WorkKind::Movement);
			if (!dists.isReachable(hex))
				continue;
			if(beamHex && hex != *beamHex)
				continue;
			// FIXME: provide distance info for Jousting bonus
			auto bai = BattleAttackInfo(activeStack, enemy, 0, callback.battleCanShoot(activeStack));
			auto attack = AttackPossibility::evaluate(bai, hex, damageCache, hb);

			attack.shootersBlockedDmg = 0; // we do not want to count on it, it is not for sure

			approaches.push_back({std::move(attack), enemy, hex, turnsToReach, multiplier});
		}
	}
	return approaches;
}
}

void BattleExchangeEvaluator::considerMovementApproach(const MovementApproach & approach,
	const battle::Unit * activeStack, PotentialTargets & targets, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & hb, const ReachabilityInfo & dists,
	uint32_t speed, bool flying, MoveTarget & result, BattleMovementTiming * timing)
{
	const auto & attack = approach.attack;
	const auto * enemy = approach.enemy;
	const auto hex = approach.hex;
	const auto turnsToReach = approach.turns;
	const auto multiplier = approach.multiplier;
	auto score = measureMovementExchange(timing, [&]()
	{
		return calculateExchange(attack, turnsToReach, targets, damageCache, hb, {},
			timing ? &timing->findMoveExchangeDetails : nullptr);
	});

	score.enemyDamageReduce *= multiplier;

#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace("Multiplier: %f, turns: %d, current score %f, new score %f", multiplier, turnsToReach, result.score, scoreValue(score));
#endif

	const float utility = scoreValue(score);
	const bool improves = result.score < utility
		|| (result.turnsToReach > turnsToReach && vstd::isAlmostEqual(result.score, utility));
	if(!improves)
		return;

	result.score = utility;
	result.positions.clear();

#if BATTLE_TRACE_LEVEL >= 1
	logAi->trace("New high score");
#endif

	BattleHex enemyHex = hex;
	while(!flying
		&& (dists.distances[enemyHex.toInt()] > speed || !dists.isReachable(enemyHex))
		&& dists.predecessors.at(enemyHex.toInt()).isValid())
	{
		battleSearch::require(hb->searchBudget().get(), battleSearch::WorkKind::Movement);
		enemyHex = dists.predecessors.at(enemyHex.toInt());

		if(dists.accessibility[enemyHex.toInt()] == EAccessibility::ALIVE_STACK)
		{
			auto defenderToBypass = hb->battleGetUnitByPos(enemyHex);
			assert(defenderToBypass != nullptr);
			auto attackHex = dists.predecessors[enemyHex.toInt()];

			if(defenderToBypass && dists.isReachable(attackHex) &&
			   defenderToBypass != enemy &&
			   vstd::contains(defenderToBypass->getAttackableHexes(activeStack), attackHex))
			{
#if BATTLE_TRACE_LEVEL >= 1
				logAi->trace("Found target to bypass at %d", enemyHex.toInt());
#endif

				auto baiBypass = BattleAttackInfo(activeStack, defenderToBypass, 0, cb->battleCanShoot(activeStack));
				auto attackBypass = AttackPossibility::evaluate(baiBypass, attackHex, damageCache, hb);

				auto adjacentStacks = getAdjacentUnits(enemy);

				adjacentStacks.push_back(defenderToBypass);
				vstd::removeDuplicates(adjacentStacks);

				auto bypassScore = measureMovementExchange(timing, [&]()
				{
					return calculateExchange(
						attackBypass,
						dists.distances[attackHex.toInt()],
						targets,
						damageCache,
						hb,
						adjacentStacks,
						timing ? &timing->findMoveExchangeDetails : nullptr);
				});

				if(scoreValue(bypassScore) > result.score)
				{
					result.score = scoreValue(bypassScore);

#if BATTLE_TRACE_LEVEL >= 1
					logAi->trace("New high score after bypass %f", scoreValue(bypassScore));
#endif
				}
			}
		}
	}
	if(!dists.isReachable(enemyHex))
		return;

	result.positions.insert(enemyHex);
	result.cachedAttack = attack;
	result.turnsToReach = turnsToReach;
}

MoveTarget BattleExchangeEvaluator::findMoveTowardsUnreachable(
	const battle::Unit * activeStack,
	PotentialTargets & targets,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> hb,
	BattleMovementTiming * timing)
{
	MovementExchangeScope measurement(timing ? &timing->findMove : nullptr);
	MoveTarget result;
	if(targets.berserk)
		return movementCandidateScreening::forcedBerserkMove(targets);

	logAi->trace("Find move towards unreachable. Enemies count %d", targets.unreachableEnemies.size());

	if(targets.unreachableEnemies.empty())
		return result;

	auto speed = activeStack->getMovementRange();

	if(speed == 0)
		return result;

	updateReachabilityMap(hb);

	battleSearch::require(hb->searchBudget().get(), battleSearch::WorkKind::Movement);
	auto dists = getReachabilityWithEnemyBypass(activeStack, damageCache, hb);
	const auto flying = activeStack->hasBonusOfType(BonusType::FLYING);
	const bool beamMovement = creatureCandidateScreening::supportsForecastBeam(activeStack, *hb, targets.berserk, false);
	const bool screenMovement = !beamMovement && creatureCandidateScreening::supportsVisibleOrdinaryMelee(activeStack, *hb);

	const auto approaches = collectApproaches(activeStack, targets, damageCache, hb, *cb, dists,
		speed, beamMovement, screenMovement);
	std::vector<float> estimates;
	estimates.reserve(approaches.size());
	for(const auto & approach : approaches)
		estimates.push_back(approach.attack.attackValue() * approach.multiplier / approach.turns);
	// Select only from admitted approaches, and retain their original evaluation
	// order so exact exchange ties preserve the existing winning destination.
	for(const auto index : movementApproachRanking::select(estimates))
		considerMovementApproach(approaches[index], activeStack, targets, damageCache,
			hb, dists, speed, flying, result, timing);
	return result;
}
