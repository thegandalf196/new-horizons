/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsReclaimer.h"
#include <algorithm>
#include "../../CPlayerState.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
namespace newHorizonsHeroes
{
bool ownsZombieDwelling(const PlayerState * ownerState, const CGHeroInstance & hero)
{
	if(!ownerState)
		return false;
	const auto walkingDead = CreatureID(CreatureID::decode("core:walkingDead"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	for(const auto * town : ownerState->getTowns())
	{
		if(!town || town->getOwner() != hero.getOwner() || town->getFactionID() != FactionID::NECROPOLIS)
			continue;
		const auto * type = town->getTown();
		if(!type)
			continue;
		const auto levels = std::min(type->creatures.size(), town->creatures.size());
		for(size_t level = 0; level < levels; ++level)
			for(size_t upgrade = 0; upgrade < type->creatures[level].size(); ++upgrade)
			{
				const auto creature = type->creatures[level][upgrade];
				if((creature != walkingDead && creature != zombie)
					|| !vstd::contains(town->creatures[level].second, creature))
					continue;
				const auto building = BuildingID::getDwellingFromLevel(static_cast<int>(level), static_cast<int>(upgrade));
				if(building.hasValue() && type->buildings.count(building) && town->hasBuilt(building))
					return true;
			}
	}
	return false;
}
}
