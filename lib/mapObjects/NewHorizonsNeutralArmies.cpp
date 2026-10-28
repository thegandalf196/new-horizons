/*
 * NewHorizonsNeutralArmies.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsNeutralArmies.h"

#include "CGCreature.h"
#include "CGHeroInstance.h"
#include "CRewardableObject.h"
#include "army/CStackInstance.h"
#include "../IGameSettings.h"
#include "../gameState/CGameState.h"
#include "../callback/IGameInfoCallback.h"
#include "../json/JsonNode.h"
#include "../mapping/CMap.h"
#include "../networkPacks/PacksForLobby.h"
#include "../rewardable/Configuration.h"

#include <stdexcept>

namespace newHorizonsNeutralArmies
{
namespace
{
int readPercentage(const JsonNode & value)
{
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 1 || value.Integer() > 100)
		throw std::runtime_error("Neutral army size percentage must be an integer in [1,100]");
	return static_cast<int>(value.Integer());
}

void scaleConfiguration(Rewardable::Configuration & configuration, int percent)
{
	for(auto & visit : configuration.info)
		for(auto & guard : visit.reward.guards)
			guard.setCount(scaleCount(guard.getCount(), percent));
}
}

int percentage(const IGameSettings & settings)
{
	return readPercentage(settings.getValue(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT));
}

TQuantity scaleCount(TQuantity count, int percent)
{
	if(count < 0 || percent < 1 || percent > 100)
		throw std::runtime_error("Invalid neutral army size scaling input");
	// TQuantity is signed 32-bit; the product is wide, and the result cannot grow.
	return static_cast<TQuantity>((static_cast<int64_t>(count) * percent + 99) / 100);
}

bool eligible(const CArmedInstance & army)
{
	const auto owner = army.getOwner();
	return (owner == PlayerColor::NEUTRAL || owner == PlayerColor::UNFLAGGABLE)
		&& dynamic_cast<const CGHeroInstance *>(&army) == nullptr;
}

TQuantity generatedCount(const CArmedInstance & army, TQuantity rawCount)
{
	return eligible(army) ? scaleCount(rawCount, percentage(army.cb->getSettings())) : rawCount;
}

void scaleGeneratedGuards(const CArmedInstance & army, Rewardable::Configuration & raw)
{
	if(!eligible(army))
		return;
	const int percent = percentage(army.cb->getSettings());
	if(percent != 100)
		scaleConfiguration(raw, percent);
}

void initializeWorld(CMap & map)
{
	const int percent = percentage(map.getSettings());
	map.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT, JsonNode(percent));
	if(percent == 100)
		return; // Preserve default army, experience and bonus-cache behavior exactly.

	for(const auto & object : map.getObjects())
	{
		auto * army = dynamic_cast<CArmedInstance *>(object);
		if(!army || !eligible(*army))
			continue;
		for(const auto & [slot, stack] : army->Slots())
			stack->setCount(scaleCount(stack->getCount(), percent));
		if(auto * monster = dynamic_cast<CGCreature *>(army))
			monster->temppower = static_cast<int64_t>(monster->getStackCount(SlotID(0))) * 1000;
		if(auto * rewardable = dynamic_cast<CRewardableObject *>(army))
			scaleConfiguration(rewardable->configuration, percent);
	}
}

void validateOverrides(const JsonNode & settings, bool supported)
{
	if(!settings.isNull() && !settings.isStruct())
		throw std::runtime_error("Saved settings must be an object");
	const auto & creatures = settings["creatures"];
	if(!creatures.isNull() && !creatures.isStruct())
		throw std::runtime_error("Saved creature settings must be an object");
	if(creatures.Struct().contains(SETTING_KEY))
	{
		const int percent = readPercentage(creatures[SETTING_KEY]);
		if(!supported && percent != 100)
			throw std::runtime_error("Reduced neutral army size requires the new save format");
	}
}

void validateSerialization(const IGameSettings & settings, bool supported)
{
	if(percentage(settings) != 100 && !supported)
		throw std::runtime_error("Reduced neutral army size requires the new save format");
}
}

void CGameState::validateNeutralArmySizeSerialization(bool supported) const
{
	if(map)
		newHorizonsNeutralArmies::validateSerialization(map->getSettings(), supported);
}

void LobbyStartGame::validateNeutralArmySizeSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNeutralArmySizeSerialization(supported);
}
