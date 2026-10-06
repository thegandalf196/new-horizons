/*
* CastleGateAction.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#include "StdInc.h"
#include "CastleGateAction.h"

#include "../../AIGateway.h"
#include "../../Engine/Nullkiller.h"
#include "../AINodeStorage.h"
#include "../../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../../lib/mapObjects/CGTownInstance.h"
#include "../../../../lib/spells/NewHorizonsMagic.h"

namespace NK2AI::AIPathfinding
{
	CastleGateAction::CastleGateAction(const CGTownInstance * source, const CGTownInstance * destination)
		: sourceTown(source)
		, destinationTown(destination)
	{
	}

	bool CastleGateAction::canAct(const Nullkiller * aiNk, const AIPathNode * source) const
	{
		return canAct(aiNk, source, source ? source->turns : 0);
	}

	bool CastleGateAction::canAct(const Nullkiller * aiNk, const AIPathNode * source, const int plannedTurn) const
	{
		if(!aiNk || !aiNk->cc || !source || !source->actor || !source->actor->hero
			|| !sourceTown || !destinationTown)
			return false;

		const auto * hero = source->actor->hero;
		if(!newHorizonsMagic::rulesActive(aiNk->cc->getMagicRules())
			|| source->coord != sourceTown->visitablePos()
			|| sourceTown->getOwner() != hero->getOwner()
			|| sourceTown->getFactionID() != FactionID::INFERNO
			|| !sourceTown->hasBuilt(BuildingSubID::CASTLE_GATE)
			|| destinationTown->getOwner() != hero->getOwner()
			|| destinationTown->getFactionID() != FactionID::INFERNO
			|| !destinationTown->hasBuilt(BuildingSubID::CASTLE_GATE)
			|| destinationTown->getVisitingHero()
			|| destinationTown == sourceTown)
			return false;

		if((dayFlagsForTurn(source, plannedTurn) & DayFlags::NEW_HORIZONS_CASTLE_GATE_USED) != DayFlags::NONE)
			return false;

		return plannedTurn != 0
			|| !hero->hasUsedNewHorizonsCastleGateToday(aiNk->cc->getCalendar().getCurrentDay());
	}

	void CastleGateAction::execute(AIGateway * aiGw, const CGHeroInstance * hero) const
	{
		if(!aiGw || !aiGw->cc || !hero || !sourceTown || !destinationTown
			|| hero->getVisitedTown() != sourceTown
			|| sourceTown->getOwner() != hero->getOwner()
			|| sourceTown->getFactionID() != FactionID::INFERNO
			|| !sourceTown->hasBuilt(BuildingSubID::CASTLE_GATE)
			|| destinationTown->getOwner() != hero->getOwner()
			|| destinationTown->getFactionID() != FactionID::INFERNO
			|| !destinationTown->hasBuilt(BuildingSubID::CASTLE_GATE)
			|| destinationTown->getVisitingHero()
			|| destinationTown == sourceTown)
			throw cannotFulfillGoalException("Castle Gate route is no longer valid.");

		const auto currentDay = aiGw->cc->getCalendar().getCurrentDay();
		if(hero->hasUsedNewHorizonsCastleGateToday(currentDay))
			throw cannotFulfillGoalException("Hero has already used a Castle Gate today.");

		if(!aiGw->cc->teleportHero(hero, destinationTown))
			throw cannotFulfillGoalException("Castle Gate teleport request was rejected.");

		aiGw->waitTillFree();
		if(hero->getVisitedTown() != destinationTown
			|| hero->movementPointsRemaining() != 0
			|| !hero->hasUsedNewHorizonsCastleGateToday(aiGw->cc->getCalendar().getCurrentDay()))
			throw cannotFulfillGoalException("Castle Gate teleport did not complete.");
	}

	void CastleGateAction::applyOnDestination(
		const CGHeroInstance *,
		CDestinationNodeInfo &,
		const PathNodeInfo & source,
		AIPathNode * destinationNode,
		const AIPathNode * sourceNode) const
	{
		destinationNode->theNodeBefore = source.node;
		destinationNode->dayFlags = static_cast<DayFlags>(
			dayFlagsForTurn(sourceNode, destinationNode->turns) | DayFlags::NEW_HORIZONS_CASTLE_GATE_USED);
	}

	std::string CastleGateAction::toString() const
	{
		return "Castle Gate to " + (destinationTown ? destinationTown->getNameTextID() : std::string("unknown town"));
	}
}
