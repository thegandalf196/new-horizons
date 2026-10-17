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
	std::set<BonusNodeType> stackNodes = {BonusNodeType::STACK_BATTLE, BonusNodeType::STACK_INSTANCE};
	if (!allowedNodes.contains(nodeType))
		return ILimiter::EDecision::NOT_APPLICABLE;

	std::optional<TerrainId> currentTerrain;

	if (stackNodes.contains(nodeType))
	{
		if (const auto * stack = context.evaluation.stackInstance())
			currentTerrain = stack->getCurrentTerrain();
	}
	else if (const auto * army = dynamic_cast<const CArmedInstance *>(&context.node))
		currentTerrain = army->getCurrentTerrain();

	const auto resolveTerrain = [&]() -> TerrainId
	{
		if(currentTerrain)
			return *currentTerrain;
		if(const auto query = context.evaluation.queryTerrain(); query && query->hasValue())
			return *query;
		if(context.evaluation.hasNativeContext())
		{
			const auto projected = context.evaluation.battleTerrain();
			if(!projected)
				throw std::logic_error("Terrain limiter requires an adventure or bound battle terrain");
			return *projected;
		}
		const auto * stack = dynamic_cast<const CStack *>(&context.node);
		if(!stack || !stack->getBattle())
			throw std::logic_error("Terrain limiter requires an adventure or bound battle terrain");
		return stack->getCurrentTerrain();
	};

	if (terrainType == ETerrainId::NATIVE_TERRAIN)
	{
		auto selector = Selector::type()(BonusType::TERRAIN_NATIVE);

		if(context.alreadyAccepted.getFirst(selector))
			return ILimiter::EDecision::ACCEPT;

		if(context.stillUndecided.getFirst(selector))
			return ILimiter::EDecision::NOT_SURE;

		return context.evaluation.faction()->toEntity(LIBRARY)->isNativeTerrain(resolveTerrain()) ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
	}

	return resolveTerrain() == terrainType ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
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
