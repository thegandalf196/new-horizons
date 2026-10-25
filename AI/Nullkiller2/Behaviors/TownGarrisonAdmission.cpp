/* TownGarrisonAdmission.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "TownGarrisonAdmission.h"
#include "../Analyzers/ArmyManager.h"
#include "../Helpers/ArmyFormation.h"
#include "../Goals/ExchangeSwapTownHeroes.h"
#include "../Goals/ExecuteHeroChain.h"
#include "../Markers/DefendTown.h"
#include "../../../lib/callback/CPlayerSpecificInfoCallback.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/TerrainTile.h"

namespace NK2AI
{
bool canPrepareArrivingTownGarrison(const CGTownInstance & town, const CGHeroInstance & hero,
	const CCreatureSet & arrivalArmy, const IArmyManager & armies, const CPlayerSpecificInfoCallback & callback)
{
	if(callback.getPlayerID() != town.getOwner() || hero.getOwner() != town.getOwner())
		return false;
	if(town.getGarrisonHero() || newHorizonsHeroes::canMergeArmies(&town, &arrivalArmy, &hero))
		return true;
	if(!armyFormation::hasLeadershipCapacityRules(&hero, &arrivalArmy)
		&& !armyFormation::hasLeadershipCapacityRules(&hero, &town))
		return false;
	const auto * tile = callback.getTile(town.visitablePos(), false);
	if(!tile)
		return false;
	armyFormation::ArmyExchangeProjection projection;
	armies.getBestArmy(&hero, &arrivalArmy, &town, tile->getTerrainID(), nullptr, &projection);
	CCreatureSet projectedVisitor;
	CCreatureSet projectedTown;
	for(const auto & slot : projection.receiverSlots)
		if(!projectedVisitor.setCreature(slot.slot, slot.creature, slot.count))
			return false;
	for(const auto & slot : projection.sourceSlots)
		if(!projectedTown.setCreature(slot.slot, slot.creature, slot.count))
			return false;
	return newHorizonsHeroes::canMergeArmies(&projectedTown, &projectedVisitor, &hero);
}


bool canPrepareCurrentTownGarrison(const CGTownInstance & town, const IArmyManager & armies,
	const CPlayerSpecificInfoCallback & callback)
{
	if(callback.getPlayerID() != town.getOwner())
		return false;
	const auto * visitor = town.getVisitingHero();
	return visitor && canPrepareArrivingTownGarrison(town, *visitor, *visitor, armies, callback);
}

void appendArrivingTownDefence(Goals::TGoalVec & sequence, const CGTownInstance & town,
	const Goals::ExecuteHeroChain & arrival, bool lockDefender, const IArmyManager & armies,
	const CPlayerSpecificInfoCallback & callback)
{
	sequence.push_back(Goals::sptr(arrival));
	if(!lockDefender)
		return;
	const auto & path = arrival.getPath();
	const bool commandable = path.targetHero && path.heroArmy
		&& canPrepareArrivingTownGarrison(town, *path.targetHero, *path.heroArmy, armies, callback);
	const auto preparation = commandable ? Goals::GarrisonPreparation::IF_COMMANDABLE
		: Goals::GarrisonPreparation::VISITING_ONLY;
	sequence.push_back(Goals::sptr(Goals::ExchangeSwapTownHeroes(
		&town, path.targetHero, HeroLockedReason::DEFENCE, preparation)));
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
