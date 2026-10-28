/*
 * MapObjectInitialization.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGameState.h"
#include "../mapping/CMap.h"
#include "../mapObjects/MiscObjects.h"
#include "../mapObjects/Quest.h"
#include "../mapObjects/NewHorizonsNeutralArmies.h"

void CGameState::initMapObjects(IGameRandomizer & gameRandomizer)
{
	logGlobal->debug("\tObject initialization");

	for(auto & obj : map->getObjects())
		obj->initObj(gameRandomizer);

	newHorizonsNeutralArmies::initializeWorld(*map);

	logGlobal->debug("\tObject initialization done");

	// getObjects<CGMonolith>() also yields CGSubterraneanGate, paired separately below instead
	for(auto & obj : map->getObjects<CGMonolith>())
		obj->assignTeleportChannel();

	// getObjects<SeerHut>() already yields exactly seer huts + quest guards
	for(auto & q : map->getObjects<SeerHut>())
		q->setObjToKill();
	CGSubterraneanGate::postInit(this); //pairing subterranean gates

	map->calculateGuardingGreaturePositions(); //calculate once again when all the guards are placed and initialized
}
