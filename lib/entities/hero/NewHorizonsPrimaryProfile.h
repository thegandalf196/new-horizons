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
/// Version 1 is the historical ten-point growth profile and remains the
/// default for saved data that predates an explicit progressionVersion field.
constexpr int PRIMARY_PROFILE_VERSION_LEGACY = 1;
/// Version 2 uses authored starting ratings plus eighteen points of growth.
constexpr int PRIMARY_PROFILE_VERSION_STARTING_AND_GROWTH = 2;
constexpr int PRIMARY_GROWTH_PER_LEVEL = 10;
constexpr int PRIMARY_PROFILE_V2_STARTING_TOTAL = 100;
constexpr int PRIMARY_PROFILE_V2_GROWTH_PER_LEVEL = 18;
/// Order is the existing PrimarySkill order: Attack, Defense, Spell Power, Knowledge.
/// A pure profile primitive, not activation of new growth in a running game.
struct DLL_LINKAGE PrimaryProfile
{
	std::array<int, GameConstants::PRIMARY_SKILLS> starting{};
	std::array<int, GameConstants::PRIMARY_SKILLS> growth{};
	/// Kept after the original members to preserve existing aggregate callers.
	/// Missing JSON progressionVersion is parsed as the legacy version 1.
	int progressionVersion = PRIMARY_PROFILE_VERSION_LEGACY;

	/// Deterministic base ratings before equipment and other explicit bonuses.
	std::array<int64_t, GameConstants::PRIMARY_SKILLS> baseAtLevel(int level) const;
};

/// Versioned tunable primary profile. Missing progressionVersion preserves the
/// historical ten-point growth formula; version 2 requires 100 starting points
/// and 18 growth points. Invalid data throws.
DLL_LINKAGE PrimaryProfile parsePrimaryProfile(const JsonNode & data);
}
