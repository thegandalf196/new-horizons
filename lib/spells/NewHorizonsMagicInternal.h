/*
 * NewHorizonsMagicInternal.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

class CGHeroInstance;

// Internal shared policy declarations. Definitions remain in NewHorizonsMagic.cpp.
namespace newHorizonsMagic::detail
{
inline constexpr std::string_view NATURE_MIRE_SHAPER = "new-horizons:natureMagic.mireShaper";
bool hasMaledictionPerk(const CGHeroInstance * hero);
int registeredSpellcraftRank(const CGHeroInstance * hero);
std::optional<size_t> schoolCoefficientRankIndex(int coefficientPercent);
}
