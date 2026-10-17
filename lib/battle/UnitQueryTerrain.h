/*
 * UnitQueryTerrain.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <optional>

class CArmedInstance;

/// Terrain-only hypothetical input, not battle/roster/hero/updater membership.
/// Missing value retains strict terrain refusal whenever terrain is required.
struct UnitQueryTerrain
{
	std::optional<TerrainId> terrain;
};

/// Capture only an ACTUAL bound battle; never guess from an adventure tile.
/// Caller retains/excludes the source world and metadata through preparation.
DLL_LINKAGE UnitQueryTerrain captureUnitQueryTerrain(const CArmedInstance * sourceArmy);
