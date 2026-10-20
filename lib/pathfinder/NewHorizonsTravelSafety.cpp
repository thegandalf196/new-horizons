/*
 * NewHorizonsTravelSafety.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CPathfinder.h"
#include "PathfinderUtil.h"
#include "PathfinderOptions.h"
#include "TurnInfo.h"
#include "NewHorizonsProtectedMobility.h"
#include "../CPlayerState.h"
#include "../IGameSettings.h"
#include "../TerrainHandler.h"
#include "../callback/IGameInfoCallback.h"
#include "../gameState/CGameState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapping/CMap.h"
#include "../spells/NewHorizonsMagic.h"

namespace
{
using newHorizonsTravel::Plan;
struct Previous
{
	int3 source;
	EPathfindingLayer layer;
	int movementRemaining;
};
Plan emptyPlan(const IGameInfoCallback & gameInfo, const int3 & source, int movement, int turn)
{
	return {source, movement, gameInfo.getCalendar().getCurrentDay() + turn, {}};
}
Plan annotateTerminal(Plan plan, const CGHeroInstance & hero)
{
	if(plan.steps.empty())
		return plan;
	auto & last = plan.steps.back();
	const auto before = plan.steps.size() == 1 ? plan.initialMovement : plan.steps[plan.steps.size() - 2].movementRemaining;
	// Readback only: exactly the existing post-accepted-move hook in
	// CGameHandler::moveHero. Raw search budgets never include this grant.
	if(last.movementRemaining == 0 && before > 0 && plan.day >= 0
		&& hero.getNewHorizonsForcedMarchLastUseDay() != plan.day
		&& hero.hasActivePerk("new-horizons:logistics", "new-horizons:logistics.forcedMarch"))
	{
		last.forcedMarch = true;
		last.movementRemaining = std::max(0, hero.movementPointsLimit()) / 10;
	}
	return plan;
}
Plan completedPlan(Plan plan, int3 destination, const std::map<int3, Previous> & previous,
	const CGHeroInstance & hero)
{
	while(destination != plan.source)
	{
		const auto & edge = previous.at(destination);
		plan.steps.push_back({destination, edge.layer, edge.movementRemaining});
		destination = edge.source;
	}
	std::reverse(plan.steps.begin(), plan.steps.end());
	return annotateTerminal(std::move(plan), hero);
}
bool ordinaryLanding(const IGameInfoCallback & gameInfo, const CGHeroInstance & hero, const int3 & position)
{
	const auto * tile = gameInfo.getTile(position);
	if(!tile || !tile->isLand() || !tile->getTerrain()->isPassable()
		|| !gameInfo.isVisibleFor(position, hero.getOwner())
		|| !gameInfo.getGuardingCreatures(position, &hero).empty()
		|| (gameInfo.getSettings().getBoolean(EGameSettings::PATHFINDER_BLOCK_DISEMBARK_ON_HOLE)
			&& gameInfo.getTileDigStatus(position) == EDiggingStatus::TILE_OCCUPIED))
		return false;
	const auto onlyHero = [&hero](const auto & objects)
	{
		return std::ranges::all_of(objects, [&hero](auto id) { return id == hero.id; });
	};
	return onlyHero(tile->blockingObjects) && onlyHero(tile->visitableObjects);
}
bool dayEndEligible(const IGameInfoCallback & gameInfo, const CGHeroInstance & hero,
	const int3 & position, EPathfindingLayer layer)
{
	if(hero.inBoat())
		return true;
	const bool requireLand = newHorizonsMagic::requiresWaterWalkLegalDayEnd(hero.getMagicRules());
	const bool requireOrdinary = layer == EPathfindingLayer::AIR
		&& newHorizonsMagic::requiresFlyLegalDayEnd(hero.getMagicRules());
	if(!requireLand && !requireOrdinary)
		return true;
	const auto * tile = gameInfo.getTile(position);
	if(!tile)
		return false;
	if(requireLand && !tile->isLand())
		return false;
	return !requireOrdinary || ordinaryLanding(gameInfo, hero, position);
}
}

std::optional<Plan> CPathfinderHelper::planWaterLanding(const int3 & source, int remainingMovement) const
{
	if(remainingMovement < 0 || !gameInfo.isInTheMap(source) || isHeroPatrolLocked())
		return std::nullopt;
	const auto * team = gameInfo.getPlayerTeam(owner);
	if(!team)
		return std::nullopt;

	// Preserve Water Walk's cost/filter/queue order; only record predecessors.
	std::multimap<int, int3> pending;
	std::map<int3, int> best;
	std::map<int3, Previous> previous;
	pending.emplace(remainingMovement, source);
	best.emplace(source, remainingMovement);
	while(!pending.empty())
	{
		auto next = std::prev(pending.end());
		const auto [movement, position] = *next;
		pending.erase(next);
		if(best.at(position) != movement)
			continue;
		const auto * sourceTile = gameInfo.getTile(position);
		NeighbourTilesVector neighbours;
		getNeighbours(*sourceTile, position, neighbours, false);
		for(const auto & destination : neighbours)
		{
			const auto * tile = gameInfo.getTile(destination);
			if(!tile || !tile->getTerrain()->isPassable()
				|| !isPatrolMovementAllowed(destination)
				|| !canMoveBetween(position, destination)
				|| isTileBlockedByHole(destination)
				|| !gameInfo.getGuardingCreatures(destination, hero).empty())
				continue;
			const auto onlyThisHero = [this](const auto & objects)
			{
				return std::all_of(objects.begin(), objects.end(), [this](auto id) { return id == hero->id; });
			};
			const bool vacatedSource = destination == hero->visitablePos()
				&& onlyThisHero(tile->blockingObjects) && onlyThisHero(tile->visitableObjects)
				&& gameInfo.isVisibleFor(destination, owner);
			const auto accessibility = tile->isWater()
				? PathfinderUtil::evaluateAccessibility<EPathfindingLayer::WATER>(destination, *tile, team->fogOfWarMap, owner, gameInfo, hero)
				: PathfinderUtil::evaluateAccessibility<EPathfindingLayer::LAND>(destination, *tile, team->fogOfWarMap, owner, gameInfo, hero);
			if(!vacatedSource && (accessibility != EPathAccessibility::ACCESSIBLE || tile->visitable()))
				continue;
			const auto layer = tile->isWater() ? EPathfindingLayer::WATER : EPathfindingLayer::LAND;
			const int cost = getMovementCost(position, destination, layer, movement, false,
				sourceTile, tile, EPathfindingLayer::WATER);
			if(cost <= 0 || cost > movement)
				continue;
			if(tile->isLand())
			{
				previous[destination] = {position, layer, movement - cost};
				return completedPlan(emptyPlan(gameInfo, source, remainingMovement, turn), destination, previous, *hero);
			}
			const int left = movement - cost;
			const auto known = best.find(destination);
			if(known == best.end() || left > known->second)
			{
				best[destination] = left;
				previous[destination] = {position, layer, left};
				pending.emplace(left, destination);
			}
		}
	}
	return std::nullopt;
}

bool CPathfinderHelper::hasSameDayLandEscape(const int3 & source, int movement) const
{
	return planWaterLanding(source, movement).has_value();
}

bool isWaterWalkDayEndLegal(const IGameInfoCallback & gameInfo, const CGHeroInstance & hero)
{
	if(hero.inBoat() || !newHorizonsMagic::requiresWaterWalkLegalDayEnd(hero.getMagicRules()))
		return true;
	const auto * tile = gameInfo.getTile(hero.visitablePos());
	return tile && tile->isLand();
}

bool areOwnedWaterWalkHeroesDayEndLegal(const IGameInfoCallback & gameInfo, PlayerColor owner)
{
	const auto * player = gameInfo.getPlayerState(owner);
	if(!player)
		return false;
	const auto heroes = player->getHeroes();
	return std::all_of(heroes.begin(), heroes.end(),
		[&gameInfo](const auto * hero) { return isWaterWalkDayEndLegal(gameInfo, *hero); });
}

std::optional<Plan> CPathfinderHelper::planAirLanding(const int3 & source, int remainingMovement, bool ordinary) const
{
	if(remainingMovement < 0 || !gameInfo.isInTheMap(source) || isHeroPatrolLocked())
		return std::nullopt;
	std::multimap<int, int3> pending{{remainingMovement, source}};
	std::map<int3, int> best{{source, remainingMovement}};
	std::map<int3, Previous> previous;
	while(!pending.empty())
	{
		auto next = std::prev(pending.end());
		const auto [movement, position] = *next;
		pending.erase(next);
		if(best.at(position) != movement)
			continue;
		// Eligibility does not emit or require a fictitious same-coordinate move.
		const bool eligible = ordinary ? ordinaryLanding(gameInfo, *hero, position) : gameInfo.getTile(position)->isLand();
		if(eligible)
			return completedPlan(emptyPlan(gameInfo, source, remainingMovement, turn), position, previous, *hero);
		const auto * sourceTile = gameInfo.getTile(position);
		NeighbourTilesVector neighbours;
		getNeighbours(*sourceTile, position, neighbours, false);
		for(const auto & destination : neighbours)
		{
			const auto * tile = gameInfo.getTile(destination);
			if(!tile || !tile->getTerrain()->isPassable()
				|| !gameInfo.isVisibleFor(destination, owner) || !isPatrolMovementAllowed(destination)
				|| !newHorizonsProtectedMobility::segmentClear(*hero, gameInfo.gameState().getMap(), position, destination))
				continue;
			// AIR transit never visits objects or initiates battles.
			const int cost = getMovementCost(position, destination, EPathfindingLayer::AIR,
				movement, false, sourceTile, tile, EPathfindingLayer::AIR);
			if(cost <= 0 || cost > movement)
				continue;
			const int left = movement - cost;
			const auto known = best.find(destination);
			if(known == best.end() || left > known->second)
			{
				best[destination] = left;
				previous[destination] = {position, EPathfindingLayer::AIR, left};
				pending.emplace(left, destination);
			}
		}
	}
	return std::nullopt;
}

bool CPathfinderHelper::hasSameDayFlightLanding(const int3 & source, int movement) const
{
	return planAirLanding(source, movement, true).has_value();
}

std::optional<Plan> CPathfinderHelper::planSameDayLanding(
	const int3 & source, int movement, EPathfindingLayer layer) const
{
	if(hero->inBoat())
		return emptyPlan(gameInfo, source, movement, turn);
	if(layer == EPathfindingLayer::WATER && newHorizonsMagic::requiresWaterWalkLegalDayEnd(hero->getMagicRules()))
		return planWaterLanding(source, movement);
	if(layer == EPathfindingLayer::AIR && !dayEndEligible(gameInfo, *hero, source, layer))
		return planAirLanding(source, movement, newHorizonsMagic::requiresFlyLegalDayEnd(hero->getMagicRules()));
	return emptyPlan(gameInfo, source, movement, turn);
}

bool CPathfinderHelper::canFinishTravelToday(const int3 & destination, int movement, EPathfindingLayer layer) const
{
	return planSameDayLanding(destination, movement, layer).has_value();
}

bool areOwnedTravelHeroesDayEndLegal(const IGameInfoCallback & gameInfo, PlayerColor owner)
{
	const auto * player = gameInfo.getPlayerState(owner);
	return player && std::ranges::all_of(player->getHeroes(), [&gameInfo](const auto * hero)
	{
		return dayEndEligible(gameInfo, *hero, hero->visitablePos(), hero->getProtectedAdventureFlightLayer());
	});
}

std::optional<newHorizonsTravel::Plan> newHorizonsTravel::planRequiredLanding(
	const IGameInfoCallback & gameInfo, const CGHeroInstance & hero)
{
	const auto source = hero.visitablePos();
	const auto movement = hero.movementPointsRemaining();
	const auto layer = hero.getProtectedAdventureFlightLayer();
	if(dayEndEligible(gameInfo, hero, source, layer))
		return emptyPlan(gameInfo, source, movement, 0);
	const PathfinderOptions options(gameInfo);
	CPathfinderHelper helper(gameInfo, &hero, options);
	const auto * info = helper.getTurnInfo();
	if(info->hasFlyingMovement())
		return helper.planSameDayLanding(source, movement, EPathfindingLayer::AIR);
	if(info->hasWaterWalking())
		return helper.planSameDayLanding(source, movement, EPathfindingLayer::WATER);
	return std::nullopt;
}
