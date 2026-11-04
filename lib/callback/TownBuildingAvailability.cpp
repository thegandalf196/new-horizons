/*
 * Part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGameInfoCallback.h"
#include "../CPlayerState.h"
#include "../IGameSettings.h"
#include "../entities/faction/CTownHandler.h"
#include "../gameState/CGameState.h"
#include "../mapObjects/CGTownInstance.h"
#include "../mapping/CMap.h"

EBuildingState CGameInfoCallback::canBuildStructure( const CGTownInstance *t, BuildingID ID ) const
{
	if(!canGetFullInfo(t))
    {
        logGlobal->error("%s: %s", BOOST_CURRENT_FUNCTION, "Town is not owned!");
        return EBuildingState::TOWN_NOT_OWNED;
    }

	if(!t->getTown()->buildings.count(ID))
		return EBuildingState::BUILDING_ERROR;

	if(t->hasBuilt(ID))	//already built
		return EBuildingState::ALREADY_PRESENT;

	//can we build it?
	if(vstd::contains(t->forbiddenBuildings, ID))
		return EBuildingState::FORBIDDEN; //forbidden

	auto possiblyNotBuiltTest = [&](const BuildingID & id) -> bool
	{
		return ((id == BuildingID::CAPITOL) ? true : !t->hasBuilt(id));
	};

	std::function<bool(BuildingID id)> allowedTest = [&](const BuildingID & id) -> bool
	{
		return !vstd::contains(t->forbiddenBuildings, id);
	};

	if (!t->genBuildingRequirements(ID, true).satisfiable(allowedTest, possiblyNotBuiltTest))
		return EBuildingState::FORBIDDEN;

	if(ID == BuildingID::CAPITOL)
	{
		const PlayerState *ps = getPlayerState(t->tempOwner, false);
		if(ps)
		{
			for(const CGTownInstance *town : ps->getTowns())
			{
				if(town->hasBuilt(BuildingID::CAPITOL))
				{
					return EBuildingState::HAVE_CAPITAL; //no more than one capitol
				}
			}
		}
	}
	else if(ID == BuildingID::SHIPYARD)
	{
		const TerrainTile *tile = getTile(t->bestLocation(), false);

		if(!tile || !tile->isWater())
			return EBuildingState::NO_WATER; //lack of water
	}

	auto buildTest = [&](const BuildingID & id) -> bool
	{
		return t->hasBuilt(id);
	};

	if (!t->genBuildingRequirements(ID).test(buildTest))
		return EBuildingState::PREREQUIRES;

	if(t->built >= getSettings().getInteger(EGameSettings::TOWNS_BUILDINGS_PER_TURN_CAP))
		return EBuildingState::CANT_BUILD_TODAY; //building limit

	//checking resources
	if(!t->getBuildingCost(ID).canBeAfforded(getPlayerState(t->tempOwner)->resources))
		return EBuildingState::NO_RESOURCES; //lack of res

	return EBuildingState::ALLOWED;
}
