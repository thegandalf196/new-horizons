/*
 * NeutralMonsterPopulation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGameHandler.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/CRandomGenerator.h"
#include "../lib/CCreatureHandler.h"
#include "../lib/mapObjects/NewHorizonsNeutralArmies.h"
#include "../lib/callback/IGameInfoCallback.h"
#include "../lib/callback/CGameInfoCallback.h"

void CGameHandler::spawnWanderingMonsters(CreatureID creatureID)
{
	std::vector<int3>::iterator tile;
	std::vector<int3> tiles;
	gameState().getFreeTiles(tiles, true);
	ui32 amount = tiles.size() / 200; //Chance is 0.5% for each tile

	RandomGeneratorUtil::randomShuffle(tiles, getRandomGenerator());
	logGlobal->trace("Spawning wandering monsters. Found %d free tiles. Creature type: %d", tiles.size(), creatureID.num);
	const CCreature *cre = creatureID.toCreature();
	for (int i = 0; i < amount; ++i)
	{
		tile = tiles.begin();
		logGlobal->trace("\tSpawning monster at %s", tile->toString());
		auto count = cre->getRandomAmount(getRandomGenerator());
		createWanderingMonster(*tile, creatureID, newHorizonsNeutralArmies::scaleCount(count,
			newHorizonsNeutralArmies::percentage(gameInfo().getSettings())));
		tiles.erase(tile); //not use it again
	}
}
