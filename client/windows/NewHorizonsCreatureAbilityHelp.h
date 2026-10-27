/*
 * NewHorizonsCreatureAbilityHelp.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../battle/SummonLifetimePresentation.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace newHorizonsCreatureAbilityHelp
{
struct FreezingTouchHelp
{
	std::string compact;
	std::string details;
};

inline std::string replace(std::string text, std::string_view token, int value)
{
	const auto replacement = std::to_string(value);
	for(size_t position = 0; (position = text.find(token, position)) != std::string::npos;)
	{
		text.replace(position, token.size(), replacement);
		position += replacement.size();
	}
	return text;
}

/// Innate ability help, not the stack's current permission to attack. No saved
/// context means no numerical claim (including out-of-game Wiki browsing).
inline std::optional<FreezingTouchHelp> freezingTouch(std::string_view creatureKey,
	const JsonNode * capturedRules, std::string compactText, std::string detailsText)
{
	if(creatureKey != "core:iceElemental" || !capturedRules)
		return std::nullopt;
	const int chance = newHorizonsFrozen::chancePercent(*capturedRules);
	if(chance <= 0)
		return std::nullopt;
	const int shatter = newHorizonsFrozen::shatterBonusPercent(*capturedRules);
	const auto format = [chance, shatter](std::string text)
	{
		return replace(replace(std::move(text), "%chance%", chance), "%bonus%", shatter);
	};
	return FreezingTouchHelp{format(std::move(compactText)), format(std::move(detailsText))};
}

inline std::vector<FreezingTouchHelp> innateAbilities(std::string_view creatureKey,
	const JsonNode * capturedRules, std::string compactText, std::string detailsText, int32_t summonRounds)
{
	std::vector<FreezingTouchHelp> result;
	if(auto freezing = freezingTouch(creatureKey, capturedRules, std::move(compactText), std::move(detailsText)))
		result.push_back(std::move(*freezing));
	if(summonRounds > 0)
		result.push_back({summonLifetimePresentation::compact(summonRounds), summonLifetimePresentation::tooltip(summonRounds)});
	return result;
}
}
