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
bool restoreTraveler(AIGateway & gateway, ObjectInstanceID id, PlayerColor owner)
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
		gateway.waitTillFree();
		hero = callback.getHero(id);
		if(!hero || hero->getOwner() != owner || !gateway.status.haveTurn()
			|| callback.getCalendar().getCurrentDay() != plan->day
			|| hero->visitablePos() != step.destination
			|| hero->movementPointsRemaining() != step.movementRemaining)
		{
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
}

bool tryEndTurnWithLegalLanding(AIGateway & gateway)
{
	const auto owner = gateway.cc->getPlayerID();
	if(!owner || !gateway.status.haveTurn())
		return false;
	std::vector<ObjectInstanceID> travelers;
	for(const auto * hero : gateway.cc->getHeroesInfo())
		if(hero && hero->getOwner() == *owner)
			travelers.push_back(hero->id);

	bool recovered = true;
	for(const auto id : travelers)
	{
		if(!gateway.status.haveTurn())
			return false;
		// Still restore other owned travelers after an unrecoverable legacy hero.
		if(!restoreTraveler(gateway, id, *owner))
			recovered = false;
	}
	// Re-read the complete owned roster: a callback may have changed it. Never
	// end the turn on the strength of a stale hero list or a stale eligibility.
	for(const auto * hero : gateway.cc->getHeroesInfo())
	{
		if(!hero || hero->getOwner() != *owner)
			continue;
		const auto plan = newHorizonsTravel::planRequiredLanding(*gateway.cc, *hero);
		if(!plan || !plan->steps.empty())
			recovered = false;
	}
	if(!recovered || !gateway.status.haveTurn())
	{
		logAi->error("Retaining turn because owned travelers could not safely land");
		return false;
	}
	gateway.cc->endTurn();
	if(gateway.status.haveTurn())
	{
		logAi->error("EndTurn was not accepted; retaining turn without retry");
		return false;
	}
	return true;
}
}
