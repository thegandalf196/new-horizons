/*
 * NewHorizonsElementalTerrain.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsElementalTerrain.h"
#include "../battle/IBattleState.h"
#include "../BattleFieldHandler.h"
#include "../GameLibrary.h"
#include "../TerrainHandler.h"

namespace newHorizonsElementalTerrain
{
namespace
{
std::string_view localKey(std::string_view key)
{
	const auto separator = key.find(':');
	return separator == std::string_view::npos ? key : key.substr(separator + 1);
}

std::optional<CreatureID> elemental(std::string_view key)
{
	const CreatureID id(CreatureID::decode(std::string(key)));
	return id.hasValue() ? std::optional<CreatureID>(id) : std::nullopt;
}
}

std::optional<CreatureID> resolve(TerrainId terrain, std::string_view battlefieldKey, std::string_view terrainKey)
{
	const auto battlefield = localKey(battlefieldKey);
	const auto terrainName = localKey(terrainKey);
	if(battlefield == "magic_plains" || battlefield == "conflux" || battlefield == "arcane")
		return elemental("core:magicElemental");
	if(battlefield == "fiery_fields" || battlefield == "lava")
		return elemental("core:fireElemental");
	if(battlefield == "rocklands" || battlefield == "subterranean" || battlefield == "rough")
		return elemental("core:earthElemental");
	if(battlefield == "magic_clouds")
		return elemental("core:airElemental");
	if(battlefield == "lucid_pools" || battlefield == "sand_shore"
		|| battlefield == "ship" || battlefield == "ship_to_ship")
		return elemental("core:waterElemental");

	if(terrainName == "wasteland")
		return elemental("core:earthElemental");
	switch(terrain.getNum())
	{
		case TerrainId::DIRT:
		case TerrainId::SAND:
		case TerrainId::ROUGH:
		case TerrainId::SUBTERRANEAN:
		case TerrainId::ROCK:
			return elemental("core:earthElemental");
		case TerrainId::GRASS:
			return elemental("core:airElemental");
		case TerrainId::SNOW:
		case TerrainId::SWAMP:
		case TerrainId::WATER:
			return elemental("core:waterElemental");
		case TerrainId::LAVA:
			return elemental("core:fireElemental");
		default:
			return std::nullopt;
	}
}

std::optional<CreatureID> primaryElemental(const IBattleInfo & battle)
{
	const auto battlefield = battle.getBattlefieldType();
	const auto * battlefieldInfo = battlefield.hasValue() ? battlefield.getInfo() : nullptr;
	const auto terrain = battle.getTerrainType();
	const auto * terrainInfo = terrain.hasValue() ? terrain.toEntity(LIBRARY) : nullptr;
	return resolve(terrain, battlefieldInfo ? battlefieldInfo->getJsonKey() : std::string(),
		terrainInfo ? terrainInfo->getJsonKey() : std::string());
}
}
