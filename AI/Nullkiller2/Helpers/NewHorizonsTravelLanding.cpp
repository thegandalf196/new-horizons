/*
 * NewHorizonsTravelLanding.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsTravelLanding.h"

#include "../AIGateway.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/pathfinder/NewHorizonsProtectedMobility.h"
#include "../../../lib/pathfinder/NewHorizonsTravelSafety.h"

#include <vector>

namespace NK2AI
{
namespace
{
bool restoreTraveler(AIGateway & gateway, ObjectInstanceID id, PlayerColor owner, bool & blockedByQuery)
{
	auto & callback = *gateway.cc;
	const auto * hero = callback.getHero(id);
	if(!hero || hero->getOwner() != owner || !gateway.status.haveTurn())
		return false;
	const auto plan = newHorizonsTravel::planRequiredLanding(callback, *hero);
	if(!plan)
	{
		logAi->error("Cannot recover traveler %d for EndTurn with current-day movement", id.getNum());
		return false;
	}
	if(plan->source != hero->visitablePos()
		|| plan->initialMovement != hero->movementPointsRemaining()
		|| plan->day != callback.getCalendar().getCurrentDay())
		return false;
	auto expectedSource = plan->source;
	int expectedMovement = plan->initialMovement;
	auto expectedLayer = hero->getProtectedAdventureFlightLayer();

	for(const auto & step : plan->steps)
	{
		// The prior wait can invalidate a replicated hero pointer. Never retain it
		// across a request, nor substitute an independently cached AI route.
		hero = callback.getHero(id);
		if(!hero || hero->getOwner() != owner || !gateway.status.haveTurn()
			|| callback.getCalendar().getCurrentDay() != plan->day
			|| hero->visitablePos() != expectedSource
			|| hero->movementPointsRemaining() != expectedMovement
			|| hero->getProtectedAdventureFlightLayer() != expectedLayer)
			return false;
		callback.moveHero(hero, hero->convertFromVisitablePos(step.destination),
			step.layer != EPathfindingLayer::LAND, step.layer);
		// A query delivered before this ack makes the authority refuse the move.
		const bool queryPending = gateway.status.getQueriesCount() > 0;
		gateway.waitTillFree();
		hero = callback.getHero(id);
		if(!hero || hero->getOwner() != owner || !gateway.status.haveTurn()
			|| callback.getCalendar().getCurrentDay() != plan->day
			|| hero->visitablePos() != step.destination
			|| hero->movementPointsRemaining() != step.movementRemaining)
		{
			if(queryPending && hero && hero->visitablePos() == expectedSource
				&& hero->movementPointsRemaining() == expectedMovement)
			{
				blockedByQuery = true; // Unchanged state: replan after the answer.
				return false;
			}
			logAi->error("Traveler %d landing request did not match its current-day plan", id.getNum());
			return false;
		}
		if(newHorizonsProtectedMobility::tracksFlight(hero->getMagicRules())
			&& hero->getProtectedAdventureFlightLayer()
				!= (step.layer == EPathfindingLayer::AIR ? EPathfindingLayer::AIR : EPathfindingLayer::LAND))
			return false;
		if(step.forcedMarch && (hero->getNewHorizonsForcedMarchLastUseDay() != plan->day
			|| hero->getNewHorizonsForcedMarchPenaltyDay() != plan->day))
			return false;
		expectedSource = step.destination;
		expectedMovement = step.movementRemaining;
		expectedLayer = hero->getProtectedAdventureFlightLayer();
	}
	const auto remaining = newHorizonsTravel::planRequiredLanding(callback, *hero);
	return remaining && remaining->steps.empty();
}

enum class EndTurnAttempt { ENDED, RETAINED, REJECTED, BLOCKED_BY_QUERY };

EndTurnAttempt attemptEndTurn(AIGateway & gateway, PlayerColor owner)
{
	std::vector<ObjectInstanceID> travelers;
	for(const auto * hero : gateway.cc->getHeroesInfo())
		if(hero && hero->getOwner() == owner)
			travelers.push_back(hero->id);

	bool recovered = true;
	bool blockedByQuery = false;
	for(const auto id : travelers)
	{
		if(!gateway.status.haveTurn())
			return EndTurnAttempt::RETAINED;
		// Still restore other owned travelers after an unrecoverable legacy hero.
		if(!restoreTraveler(gateway, id, owner, blockedByQuery))
			recovered = false;
	}
	// Re-read the complete owned roster: a callback may have changed it. Never
	// end the turn on the strength of a stale hero list or a stale eligibility.
	for(const auto * hero : gateway.cc->getHeroesInfo())
	{
		if(!hero || hero->getOwner() != owner)
			continue;
		const auto plan = newHorizonsTravel::planRequiredLanding(*gateway.cc, *hero);
		if(!plan || !plan->steps.empty())
			recovered = false;
	}
	if(!recovered || !gateway.status.haveTurn())
	{
		if(blockedByQuery && gateway.status.haveTurn())
			return EndTurnAttempt::BLOCKED_BY_QUERY;
		logAi->error("Retaining turn because owned travelers could not safely land");
		return EndTurnAttempt::RETAINED;
	}
	gateway.cc->endTurn();
	if(!gateway.status.haveTurn())
		return EndTurnAttempt::ENDED;
	// The authority sends a query (e.g. a post-battle level-up issued after
	// BattleEnded) before this rejection, so a refusal caused by a query this
	// controller has not answered yet is always visible here.
	return gateway.status.getQueriesCount() > 0 ? EndTurnAttempt::BLOCKED_BY_QUERY : EndTurnAttempt::REJECTED;
}
}

bool tryEndTurnWithLegalLanding(AIGateway & gateway)
{
	// Each retry follows a query that blocked this attempt and is answered first.
	constexpr int MAX_QUERY_BLOCKED_ATTEMPTS = 4;
	const auto owner = gateway.cc->getPlayerID();
	if(!owner || !gateway.status.haveTurn())
		return false;
	for(int attempt = 1;; ++attempt)
	{
		const auto result = attemptEndTurn(gateway, *owner);
		if(result == EndTurnAttempt::ENDED)
			return true;
		if(result == EndTurnAttempt::RETAINED)
			return false;
		if(result == EndTurnAttempt::REJECTED)
		{
			// A player eliminated during its own turn no longer owns a turn to end.
			if(gateway.cc->getPlayerStatus(*owner) != EPlayerStatus::INGAME)
				logAi->info("EndTurn refused after this player left the game");
			else
				logAi->error("EndTurn was not accepted; retaining turn without retry");
			return false;
		}
		if(attempt == MAX_QUERY_BLOCKED_ATTEMPTS)
		{
			logAi->error("Retaining turn after %d query-blocked EndTurn attempts", attempt);
			return false;
		}
		logAi->info("EndTurn waits for %d pending queries before retrying", gateway.status.getQueriesCount());
		gateway.waitTillFree();
	}
}
}
