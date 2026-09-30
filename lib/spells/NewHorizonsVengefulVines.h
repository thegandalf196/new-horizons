/*
 * NewHorizonsVengefulVines.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../battle/BattleHexArray.h"
#include "../battle/Destination.h"
#include "../constants/EntityIdentifiers.h"
#include "../json/JsonNode.h"

#include <string_view>

namespace newHorizonsVengefulVines
{
inline constexpr std::string_view SPELL_KEY = "new-horizons:vengefulVines";

/// True only for the active Vengeful Vines entry in a saved New Horizons v3 roster.
DLL_LINKAGE bool enabled(const JsonNode & savedRules, SpellID spell);

/// Returns the selected origin and five winding steps, or empty if any step is unavailable.
DLL_LINKAGE BattleHexArray footprint(const BattleHex & origin, BattleHex::EDir direction);

/// Reads exactly two location destinations: origin, then an adjacent direction marker.
/// Unit targets, malformed orientations, and footprints that do not fit return empty.
DLL_LINKAGE BattleHexArray footprint(const battle::Target & target);
}
