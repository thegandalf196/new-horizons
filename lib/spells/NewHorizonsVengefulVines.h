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
#include <vector>

namespace newHorizonsVengefulVines
{
inline constexpr std::string_view SPELL_KEY = "new-horizons:vengefulVines";

/// True only for the active Vengeful Vines entry in a saved New Horizons v3 roster.
DLL_LINKAGE bool enabled(const JsonNode & savedRules, SpellID spell);

/// Reads exactly three ordered, distinct, playable location destinations.
/// Each destination after the first must touch at least one earlier selection.
/// Unit targets, malformed order, and unavailable hexes return empty.
DLL_LINKAGE BattleHexArray footprint(const battle::Target & target);

/// Returns each connected three-hex selection once, in a deterministic valid click order.
/// Every result is suitable for constructing three ordered location destinations.
DLL_LINKAGE const std::vector<BattleHexArray> & connectedTriples();
}
