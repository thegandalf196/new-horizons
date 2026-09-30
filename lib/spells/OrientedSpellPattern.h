/*
 * OrientedSpellPattern.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <span>

#include "../battle/BattleHexArray.h"

namespace spells
{
/// Builds a path from origin, applying each offset to the selected direction modulo six.
/// Returns an empty path if the origin or any step is unavailable, or a hex is revisited.
DLL_LINKAGE BattleHexArray makeOrientedSpellPath(const BattleHex & origin, BattleHex::EDir direction,
	std::span<const int> relativeDirections);
/// Returns the six-way direction from origin to an adjacent endpoint, or BattleHex::NONE.
DLL_LINKAGE BattleHex::EDir adjacentSpellDirection(const BattleHex & origin, const BattleHex & endpoint);
}
