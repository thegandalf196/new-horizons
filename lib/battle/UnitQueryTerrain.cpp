/*
 * UnitQueryTerrain.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitQueryTerrain.h"
#include "BattleInfo.h"
#include "../mapObjects/army/CArmedInstance.h"

UnitQueryTerrain captureUnitQueryTerrain(const CArmedInstance * sourceArmy)
{
	if(!sourceArmy || !sourceArmy->battle)
		return {};
	const auto terrain = sourceArmy->battle->getTerrainType();
	return {terrain.hasValue() ? std::optional<TerrainId>(terrain) : std::nullopt};
}
