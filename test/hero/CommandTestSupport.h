/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/entities/hero/NewHorizonsCommandState.h"

namespace commandTest
{
inline newHorizonsHeroes::CommandRules rules(uint32_t interval = 3)
{
	newHorizonsHeroes::CommandRules result;
	result.levelInterval = interval;
	result.authorityRankBonuses = {0, 1, 2, 3};
	result.enhancements = {{{20, 1}, {20, 1}, {20, 1}, {10, 1}, {10, 1}, {10, 1}, {10, 1}, {10, 5}, {10, 5}}};
	result.authorityPerkValues = {2, 2, 10, 1, 2, 1, 1, 10, 15, 10};
	return result;
}

inline JsonNode policy()
{
	const auto values = rules();
	JsonNode result;
	result["version"].Integer() = 1;
	result["classLevelIntervals"]["core:knight"].Integer() = 3;
	result["classLevelIntervals"]["core:cleric"].Integer() = 4;
	for(const auto bonus : values.authorityRankBonuses)
		result["authorityRankBonuses"].Vector().emplace_back(bonus);
	for(size_t i = 0; i < values.enhancements.size(); ++i)
	{
		auto & formula = result["enhancements"][std::string(newHorizonsHeroes::COMMAND_ENHANCEMENT_KEYS[i])];
		formula["levelDivisor"].Integer() = values.enhancements[i].levelDivisor;
		formula["multiplier"].Integer() = values.enhancements[i].multiplier;
	}
	for(size_t i = 0; i < values.authorityPerkValues.size(); ++i)
		result["authorityPerks"][std::string(newHorizonsHeroes::AUTHORITY_PERK_KEYS[i])].Integer() = values.authorityPerkValues[i];
	return result;
}

inline JsonNode capabilities()
{
	JsonNode result;
	result["armyCommand"] = newHorizonsHeroes::resolveArmyCommandPolicy(policy(), "core:knight");
	return result;
}
}
