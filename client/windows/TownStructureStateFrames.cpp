/*
 * TownStructureStateFrames.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "TownStructureStateFrames.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTown.h"
#include "../../lib/mapObjects/CGTownInstance.h"

namespace townStructureStateFrames
{
TownStructureStateFrames forStructure(const CGTownInstance & town, const CStructure & structure)
{
	if(!structure.building)
		return {};

	const BuildingID building = structure.building->bid;

	// The shipyard DEF holds the shipyard without the citadel's moat (frame 0) and with it (frame 1)
	if(town.getFactionID() == FactionID::CASTLE && (building == BuildingID::SHIPYARD || building == BuildingID::SHIP))
	{
		if(town.hasBuilt(BuildingID::CITADEL))
			return {1, std::nullopt};
		return {std::nullopt, 0};
	}

	// The Mana Vortex DEF holds its animation without Mage Guild 5 (frames 0-8) and with it (from frame 10)
	if(town.getFactionID() == FactionID::DUNGEON && building == BuildingID::SPECIAL_2)
	{
		if(town.hasBuilt(BuildingID::MAGES_GUILD_5))
			return {10, std::nullopt};
		return {std::nullopt, 9};
	}

	return {};
}
}
