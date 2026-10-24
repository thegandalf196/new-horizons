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
#include <cstdint>
#include <optional>
#include <string_view>

class CGHeroInstance;
class JsonNode;

// Internal shared policy declarations; no exported scripting or facade API.
namespace newHorizonsMagic::detail
{
int32_t quicksandSpellPowerStep(const JsonNode & rules, int32_t casterPowerDivisor);
void validateQuicksandSpellRow(const JsonNode & rules, std::string_view spellIdentity, const JsonNode & row);
inline constexpr std::string_view NATURE_MIRE_SHAPER = "new-horizons:natureMagic.mireShaper";
bool hasMaledictionPerk(const CGHeroInstance * hero);
int registeredSpellcraftRank(const CGHeroInstance * hero);
std::optional<size_t> schoolCoefficientRankIndex(int coefficientPercent);
}
