/*
 * NewHorizonsEagleEye.h, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <string_view>
#include <vector>

class CGHeroInstance;

namespace newHorizonsEagleEye
{
constexpr std::string_view SKILL_ID = "new-horizons:learning";
constexpr std::string_view PERK_ID = "new-horizons:learning.eagleEye";

DLL_LINKAGE bool enabled(const CGHeroInstance * hero);
/// Ordered accepted enemy HERO_SPELL history. No RNG or learning mutation.
/// Highest eligible Level 1-3 wins; equal-level ties retain the earliest cast.
DLL_LINKAGE std::optional<SpellID> selectSpell(const CGHeroInstance * hero,
	const std::vector<SpellID> & enemyHeroCasts);
}
