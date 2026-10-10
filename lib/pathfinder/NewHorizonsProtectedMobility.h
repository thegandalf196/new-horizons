/*
 * NewHorizonsProtectedMobility.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#pragma once
#include "../int3.h"
#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"
#include <vector>

class CGHeroInstance;
class CMap;

namespace newHorizonsProtectedMobility
{
struct DLL_LINKAGE FlightReceipt
{
	EPathfindingLayer source = EPathfindingLayer::LAND;
	EPathfindingLayer destination = EPathfindingLayer::AIR;
	void validate() const;
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validate();
		h & source;
		h & destination;
		if(!h.saving)
			validate();
	}
};
DLL_LINKAGE bool enabled(const JsonNode & rules);
DLL_LINKAGE EPathfindingLayer displacementLayer(const CGHeroInstance & hero, EPathfindingLayer layer);
DLL_LINKAGE void validateRulesSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE void validateTiles(const std::vector<int3> & tiles, int width, int height, int levels);
DLL_LINKAGE std::vector<int3> readTiles(const JsonNode & value, int width, int height, int levels);
DLL_LINKAGE JsonNode writeTiles(const std::vector<int3> & tiles);
/// Cell-centre supercover, including both closed-corner neighbours. No terrain inference.
DLL_LINKAGE bool segmentClear(const std::vector<int3> & tiles, const int3 & source, const int3 & destination);
DLL_LINKAGE bool segmentClear(const CGHeroInstance & hero, const CMap & map,
	const int3 & source, const int3 & destination);
}
