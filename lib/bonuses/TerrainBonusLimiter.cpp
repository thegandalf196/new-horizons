/*
 * TerrainBonusLimiter.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "Limiters.h"
#include "../CStack.h"
#include "../GameLibrary.h"
#include "../TerrainHandler.h"
#include "../entities/faction/CTownHandler.h"
#include "../mapObjects/army/CStackInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../json/JsonNode.h"
#include <stdexcept>

TerrainLimiter::TerrainLimiter()
	: terrainType(ETerrainId::NATIVE_TERRAIN)
{
}

TerrainLimiter::TerrainLimiter(TerrainId terrain):
	terrainType(terrain)
{
}

ILimiter::EDecision TerrainLimiter::limit(const BonusLimitationContext &context) const
{
	BonusNodeType nodeType = context.node.getNodeType();
	std::set<BonusNodeType> allowedNodes = {BonusNodeType::STACK_BATTLE, BonusNodeType::STACK_INSTANCE, BonusNodeType::HERO, BonusNodeType::TOWN, BonusNodeType::TOWN_AND_VISITOR};
	if (!allowedNodes.contains(nodeType))
		return ILimiter::EDecision::NOT_APPLICABLE;
	const auto currentTerrain = context.evaluation.adventureTerrain();

	if (terrainType == ETerrainId::NATIVE_TERRAIN)
	{
		auto selector = Selector::type()(BonusType::TERRAIN_NATIVE);

		if(context.alreadyAccepted.getFirst(selector))
			return ILimiter::EDecision::ACCEPT;

		if(context.stillUndecided.getFirst(selector))
			return ILimiter::EDecision::NOT_SURE;

		return context.evaluation.nativeTerrainMatches(currentTerrain) ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
	}

	return context.evaluation.resolveTerrain(currentTerrain) == terrainType ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
}

std::string TerrainLimiter::toString() const
{
	boost::format fmt("CreatureTerrainLimiter(terrainType=%s)");
	if(terrainType == ETerrainId::NATIVE_TERRAIN)
		fmt % "native";
	else
		fmt % LIBRARY->terrainTypeHandler->getById(terrainType)->getJsonKey();
	return fmt.str();
}

JsonNode TerrainLimiter::toJsonNode() const
{
	if(terrainType == ETerrainId::NATIVE_TERRAIN)
		return JsonNode("CREATURE_NATIVE_TERRAIN");
	JsonNode root;

	root["type"].String() = "CREATURE_TERRAIN_LIMITER";
	auto terrainName = LIBRARY->terrainTypeHandler->getById(terrainType)->getJsonKey();
	root["parameters"].Vector().emplace_back(terrainName);

	return root;
}
