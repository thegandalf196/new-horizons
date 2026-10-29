/*
 * NewHorizonsCreatureAbilityHelp.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../battle/SummonLifetimePresentation.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace newHorizonsCreatureAbilityHelp
{
struct InnateAbilityHelp
{
	std::string compact;
	std::string details;
	std::string iconPath;
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
inline std::optional<InnateAbilityHelp> freezingTouch(std::string_view creatureKey,
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
	return InnateAbilityHelp{format(std::move(compactText)), format(std::move(detailsText)), {}};
}

/// Describes intrinsic identity, not whether this stack may currently harvest.
inline std::optional<InnateAbilityHelp> soulHarvest(std::string_view creatureKey,
	std::string compactText, std::string detailsText)
{
	if(creatureKey != "new-horizons:ghost")
		return std::nullopt;
	return InnateAbilityHelp{
		replace(std::move(compactText), "%count%", newHorizonsSoulHarvest::GHOSTS_PER_CREATURE_KILLED),
		replace(std::move(detailsText), "%count%", newHorizonsSoulHarvest::GHOSTS_PER_CREATURE_KILLED),
		"newHorizons/heroSpecialties/VidominaSoulHarvest/icon-44.png"};
}

inline std::vector<InnateAbilityHelp> innateAbilities(std::string_view creatureKey,
	const JsonNode * capturedRules, std::string compactText, std::string detailsText, int32_t summonRounds,
	std::string soulHarvestCompact, std::string soulHarvestDetails)
{
	std::vector<InnateAbilityHelp> result;
	if(auto freezing = freezingTouch(creatureKey, capturedRules, std::move(compactText), std::move(detailsText)))
		result.push_back(std::move(*freezing));
	if(auto harvest = soulHarvest(creatureKey, std::move(soulHarvestCompact), std::move(soulHarvestDetails)))
		result.push_back(std::move(*harvest));
	if(summonRounds > 0)
		result.push_back({summonLifetimePresentation::compact(summonRounds), summonLifetimePresentation::tooltip(summonRounds), "NH_spell_time_stop-44.png"});
	return result;
}

/// Bind shared localized text for the creature window and encyclopedia.
std::vector<InnateAbilityHelp> translatedInnateAbilities(std::string_view creatureKey,
	const JsonNode * capturedRules, int32_t summonRounds);

}
