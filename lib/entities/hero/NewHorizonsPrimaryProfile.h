/*
 * NewHorizonsPrimaryProfile.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../GameConstants.h"
#include "../../json/JsonNode.h"

namespace newHorizonsHeroes
{
constexpr int PRIMARY_GROWTH_PER_LEVEL = 10;
/// Order is the existing PrimarySkill order: Attack, Defense, Spell Power, Knowledge.
/// A pure profile primitive, not activation of new growth in a running game.
struct DLL_LINKAGE PrimaryProfile
{
	std::array<int, GameConstants::PRIMARY_SKILLS> starting{};
	std::array<int, GameConstants::PRIMARY_SKILLS> growth{};

	/// Deterministic base ratings, before independent skill bonuses/artifacts.
	std::array<int64_t, GameConstants::PRIMARY_SKILLS> baseAtLevel(int level) const;
};

/// Tunable starting ratings and four positive growth increments totaling ten.
/// Accepts both 4/3/2/1 and class-specific ten-point profiles without a random
/// tie-breaker or a hidden redistribution step. Invalid data throws.
DLL_LINKAGE PrimaryProfile parsePrimaryProfile(const JsonNode & data);
}
