/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsRecruitmentGrowth.h"
#include "../IGameSettings.h"
#include "../json/JsonNode.h"
#include "../mapping/CMap.h"
#include "../gameState/CGameState.h"
#include "../networkPacks/PacksForLobby.h"
#include <stdexcept>
#include <limits>

namespace newHorizonsRecruitmentGrowth
{
namespace
{
int readPercentage(const JsonNode & value)
{
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 0 || value.Integer() > 1000)
		throw std::runtime_error("Recruitment growth percentage must be an integer in [0,1000]");
	return static_cast<int>(value.Integer());
}
}
int percentage(const IGameSettings & settings)
{
	return readPercentage(settings.getValue(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT));
}
int64_t scale(int64_t count, int percent)
{
	if(count < 0 || percent < 0 || percent > 1000)
		throw std::runtime_error("Invalid recruitment growth scaling input");
	if(percent == 0)
		return 0;
	const auto tail = (count % 100 * percent + 99) / 100;
	if(count / 100 > (std::numeric_limits<int64_t>::max() - tail) / percent)
		throw std::overflow_error("Recruitment growth exceeds supported count range");
	return count / 100 * percent + tail;
}
uint32_t generatedAmount(int64_t count, const IGameSettings & settings)
{
	const auto result = scale(count, percentage(settings));
	if(result > std::numeric_limits<uint32_t>::max())
		throw std::overflow_error("Recruitment amount exceeds stored stock range");
	return static_cast<uint32_t>(result);
}
uint32_t stockAfterWeek(uint32_t previous, int64_t growth, int percent, bool doubleGrowth, bool plague)
{
	if(growth < 0)
		throw std::runtime_error("Invalid negative recruitment growth");
	if(plague)
		return previous / 2;
	const int multiplier = doubleGrowth ? 2 : 1;
	if(growth > std::numeric_limits<int64_t>::max() / multiplier)
		throw std::overflow_error("Calendar recruitment growth exceeds supported range");
	const auto increment = scale(growth * multiplier, percent);
	const auto stock = static_cast<int64_t>(previous) * multiplier;
	if(increment > std::numeric_limits<uint32_t>::max() - stock)
		throw std::overflow_error("Recruitment stock exceeds supported count range");
	return static_cast<uint32_t>(stock + increment);
}
void initializeWorld(CMap & map)
{
	map.overrideGameSetting(EGameSettings::CREATURES_RECRUITMENT_GROWTH_PERCENT,
		JsonNode(percentage(map.getSettings())));
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
		const auto percent = readPercentage(creatures[SETTING_KEY]);
		if(!supported && percent != 100)
			throw std::runtime_error("Reduced recruitment growth requires its captured save format");
	}
}
void validateSerialization(const IGameSettings & settings, bool supported)
{
	if(percentage(settings) != 100 && !supported)
		throw std::runtime_error("Reduced recruitment growth requires its captured save format");
}
}
void CGameState::validateRecruitmentGrowthSerialization(bool supported) const
{
	if(map)
		newHorizonsRecruitmentGrowth::validateSerialization(map->getSettings(), supported);
}
void LobbyStartGame::validateRecruitmentGrowthSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateRecruitmentGrowthSerialization(supported);
}
