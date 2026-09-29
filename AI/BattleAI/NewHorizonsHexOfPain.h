/*
 * NewHorizonsHexOfPain.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <string_view>

namespace battle
{
class Unit;
}

namespace newHorizonsHexOfPainAI
{
inline constexpr std::string_view SPELL_ID = "new-horizons:hexOfPain";
inline constexpr std::string_view TRIGGER_ID = "core:hexOfPain";

bool hasEffect(const battle::Unit * unit);
int effectRounds(const battle::Unit * unit);
}
