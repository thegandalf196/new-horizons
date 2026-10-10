/*
 * NewHorizonsProtectedMobility.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"
#include "NewHorizonsProtectedMobility.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapping/CMap.h"
#include "../spells/NewHorizonsMagic.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace newHorizonsProtectedMobility
{
void FlightReceipt::validate() const
{
	if((source != EPathfindingLayer::LAND && source != EPathfindingLayer::AIR)
		|| (destination != EPathfindingLayer::LAND && destination != EPathfindingLayer::AIR
			&& destination != EPathfindingLayer::WATER)
		|| (source != EPathfindingLayer::AIR && destination != EPathfindingLayer::AIR))
		throw std::runtime_error("Invalid protected adventure flight receipt");
}
void validateRulesSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("protectedAdventureBarriers"))
		return;
	if(!supported || !rules["protectedAdventureBarriers"].isBool()
		|| !rules["rulesetVersion"].isNumber()
		|| rules["rulesetVersion"].Float() != newHorizonsMagic::CURRENT_RULESET_VERSION)
		throw std::runtime_error("Unsupported or invalid protected adventure barrier rules");
}

bool enabled(const JsonNode & rules)
{
	validateRulesSerialization(rules, true);
	return rules.isStruct() && rules.Struct().contains("protectedAdventureBarriers")
		&& rules["protectedAdventureBarriers"].Bool();
}

EPathfindingLayer displacementLayer(const CGHeroInstance & hero, EPathfindingLayer layer)
{
	return enabled(hero.getMagicRules()) && !hero.inBoat() && layer == EPathfindingLayer::AIR
		? EPathfindingLayer::LAND : layer;
}

void validateTiles(const std::vector<int3> & tiles, int width, int height, int levels)
{
	for(size_t i = 0; i < tiles.size(); ++i)
	{
		const auto & tile = tiles[i];
		if(tile.x < 0 || tile.y < 0 || tile.z < 0 || tile.x >= width || tile.y >= height || tile.z >= levels
			|| (i && !(tiles[i - 1] < tile)))
			throw std::runtime_error("Invalid protected adventure mobility tiles");
	}
}

std::vector<int3> readTiles(const JsonNode & value, int width, int height, int levels)
{
	if(!value.isVector())
		throw std::runtime_error("Protected adventure mobility tiles must be an array");
	std::vector<int3> result;
	for(const auto & row : value.Vector())
	{
		if(!row.isVector() || row.Vector().size() != 3)
			throw std::runtime_error("Protected adventure mobility tile must be a coordinate triple");
		const int bounds[] = {width, height, levels};
		int coordinates[3];
		for(size_t axis = 0; axis < 3; ++axis)
		{
			const auto & coordinate = row.Vector()[axis];
			if(!coordinate.isNumber() || !std::isfinite(coordinate.Float())
				|| coordinate.Float() < 0 || coordinate.Float() >= bounds[axis]
				|| coordinate.Float() != std::floor(coordinate.Float()))
				throw std::runtime_error("Protected adventure mobility coordinate is not a bounded integer");
			coordinates[axis] = static_cast<int>(coordinate.Float());
		}
		result.emplace_back(coordinates[0], coordinates[1], coordinates[2]);
	}
	std::sort(result.begin(), result.end());
	validateTiles(result, width, height, levels);
	return result;
}

JsonNode writeTiles(const std::vector<int3> & tiles)
{
	JsonNode result;
	result.setType(JsonNode::JsonType::DATA_VECTOR);
	for(const auto & tile : tiles)
	{
		JsonNode row;
		row.setType(JsonNode::JsonType::DATA_VECTOR);
		for(int coordinate : {tile.x, tile.y, tile.z})
		{
			JsonNode value;
			value.Float() = coordinate;
			row.Vector().push_back(std::move(value));
		}
		result.Vector().push_back(std::move(row));
	}
	return result;
}

bool segmentClear(const std::vector<int3> & tiles, const int3 & source, const int3 & destination)
{
	if(source.z != destination.z || source.x < 0 || source.y < 0 || source.z < 0
		|| destination.x < 0 || destination.y < 0)
		return false;
	const auto clear = [&tiles](const int3 & position)
	{
		return !std::binary_search(tiles.begin(), tiles.end(), position);
	};
	int3 position = source;
	if(!clear(position))
		return false;
	const int64_t nx = std::abs(static_cast<int64_t>(destination.x) - source.x);
	const int64_t ny = std::abs(static_cast<int64_t>(destination.y) - source.y);
	const int sx = destination.x > source.x ? 1 : -1;
	const int sy = destination.y > source.y ? 1 : -1;
	int64_t ix = 0, iy = 0;
	while(ix < nx || iy < ny)
	{
		const int64_t xCrossing = (2 * ix + 1) * ny;
		const int64_t yCrossing = (2 * iy + 1) * nx;
		if(ix == nx || (iy < ny && xCrossing > yCrossing))
		{
			position.y += sy;
			++iy;
		}
		else if(iy == ny || xCrossing < yCrossing)
		{
			position.x += sx;
			++ix;
		}
		else
		{
			if(!clear(position + int3(sx, 0, 0)) || !clear(position + int3(0, sy, 0)))
				return false;
			position.x += sx;
			position.y += sy;
			++ix;
			++iy;
		}
		if(!clear(position))
			return false;
	}
	return true;
}

bool segmentClear(const CGHeroInstance & hero, const CMap & map,
	const int3 & source, const int3 & destination)
{
	return !enabled(hero.getMagicRules()) || (map.isInTheMap(source) && map.isInTheMap(destination)
		&& segmentClear(map.getProtectedAdventureMobilityTiles(), source, destination));
}
}
