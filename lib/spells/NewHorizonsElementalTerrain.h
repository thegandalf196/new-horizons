/*
 * NewHorizonsElementalTerrain.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <string_view>

class IBattleInfo;

namespace newHorizonsElementalTerrain
{
/// Pure authored-table lookup. Special battlefield identities override terrain;
/// unknown custom terrain is deliberately not assigned an invented element.
DLL_LINKAGE std::optional<CreatureID> resolve(TerrainId terrain, std::string_view battlefieldKey,
	std::string_view terrainKey = {});
/// Uses the captured battle context, never the hero's current adventure tile.
DLL_LINKAGE std::optional<CreatureID> primaryElemental(const IBattleInfo & battle);
}
