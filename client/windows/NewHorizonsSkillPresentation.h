/*
 * NewHorizonsSkillPresentation.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/json/JsonNode.h"
#include "../../lib/battle/NewHorizonsSylvanTactics.h"

#include <array>
#include <string>
#include <string_view>

namespace newHorizonsSkillPresentation
{
/// A display-only Dungeon rename follows the installed translation even for
/// old mechanics captures. Every other captured perk-catalogue name is retained.
inline std::string perkSkillName(std::string_view skillId, const std::string & installed,
	const std::string & captured)
{
	return skillId == "new-horizons:shroudOfMalassa" || captured.empty() ? installed : captured;
}

inline std::string skillName(const JsonNode & rules, std::string_view skillId,
	const std::string & fallback)
{
	return skillId == "new-horizons:sylvanLuck"
		? newHorizonsSylvanTactics::capturedSkillName(rules) : fallback;
}

/// Presentation reads the supplied snapshot only. Missing legacy entries keep
/// their ordinary entity text rather than borrowing installed perk rules.
inline std::string rankDescription(const JsonNode & rules, std::string_view skillId,
	int rank, const std::string & fallback)
{
	if(skillId == "new-horizons:sylvanLuck")
		return newHorizonsSylvanTactics::capturedRankDescription(rules, rank);
	// Other Skills keep their existing translated presentation unchanged.
	if(skillId != "new-horizons:necromancy" || rank < 1 || rank > 3
		|| !rules.isStruct() || !rules["skills"].isStruct())
		return fallback;
	const auto & skills = rules["skills"].Struct();
	const auto skill = skills.find(std::string(skillId));
	if(skill == skills.end() || !skill->second.isStruct())
		return fallback;
	const auto & ranks = skill->second["ranks"];
	if(!ranks.isStruct())
		return fallback;
	constexpr std::array<const char *, 3> rankKeys = {"basic", "advanced", "expert"};
	const auto & entry = ranks[rankKeys[static_cast<size_t>(rank - 1)]];
	if(!entry.isStruct())
		return fallback;
	const auto & description = entry["description"];
	return description.isString() && !description.String().empty() ? description.String() : fallback;
}

/// Shared multi-rank assembly for the encyclopedia; the caller supplies the
/// current world's registry (or installation defaults before a world exists).
inline std::string allRankDescriptions(const JsonNode & rules, std::string_view skillId,
	const std::array<std::string, 3> & fallback)
{
	std::string result;
	for(int rank = 1; rank <= 3; ++rank)
	{
		const auto description = rankDescription(rules, skillId, rank, fallback[static_cast<size_t>(rank - 1)]);
		if(!description.empty())
		{
			if(!result.empty())
				result += "\n\n";
			result += description;
		}
	}
	return result;
}
}
