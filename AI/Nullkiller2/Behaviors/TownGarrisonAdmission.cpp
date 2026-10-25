/* TownGarrisonAdmission.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "TownGarrisonAdmission.h"
#include "../Analyzers/ArmyManager.h"
#include "../Helpers/ArmyFormation.h"
#include "../Goals/ExchangeSwapTownHeroes.h"
#include "../Markers/DefendTown.h"
#include "../../../lib/callback/CPlayerSpecificInfoCallback.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/TerrainTile.h"

namespace NK2AI
{
bool canPrepareCurrentTownGarrison(const CGTownInstance & town, const IArmyManager & armies,
	const CPlayerSpecificInfoCallback & callback)
{
	if(callback.getPlayerID() != town.getOwner())
		return false;
	const auto * visitor = town.getVisitingHero();
	if(!visitor || visitor->getOwner() != town.getOwner())
		return false;
	if(armyFormation::canSwapGarrisonHero(&town))
		return true;
	if(!armyFormation::hasLeadershipCapacityRules(visitor, visitor)
		&& !armyFormation::hasLeadershipCapacityRules(visitor, &town))
		return false;
	const auto * tile = callback.getTile(town.visitablePos(), false);
	if(!tile)
		return false;
	armyFormation::ArmyExchangeProjection projection;
	armies.getBestArmy(visitor, visitor, &town, tile->getTerrainID(), nullptr, &projection);
	CCreatureSet projectedVisitor;
	CCreatureSet projectedTown;
	for(const auto & slot : projection.receiverSlots)
		if(!projectedVisitor.setCreature(slot.slot, slot.creature, slot.count))
			return false;
	for(const auto & slot : projection.sourceSlots)
		if(!projectedTown.setCreature(slot.slot, slot.creature, slot.count))
			return false;
	return newHorizonsHeroes::canMergeArmies(&projectedTown, &projectedVisitor, visitor);
}

std::optional<Goals::Composition> currentVisitorGarrisonDefence(const CGTownInstance & town,
	const HitMapInfo & threat, const IArmyManager & armies, const CPlayerSpecificInfoCallback & callback)
{
	if(!canPrepareCurrentTownGarrison(town, armies, callback))
		return std::nullopt;
	Goals::Composition result;
	result.addNext(Goals::DefendTown(&town, threat, town.getVisitingHero()))
		.addNext(Goals::ExchangeSwapTownHeroes(&town, town.getVisitingHero(), HeroLockedReason::DEFENCE));
	return result;
}
}
