/*
 * NewHorizonsHistoricalAdventurePolicyTestUtils.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../lib/IGameSettings.h"
#include "../lib/json/JsonNode.h"
#include "../lib/mapping/CMap.h"

/// Capture the preceding policy for an existing positive old-format fixture.
/// Read its already authored map context, not mutable installed configuration.
inline void isolateHistoricalAdventurePolicies(CMap & map)
{
	auto capabilities = map.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES);
	if(capabilities.isStruct())
	{
		capabilities.Struct().erase("artifactManaRegeneration");
		capabilities.Struct().erase("glyphsOfFearAura");
		capabilities.setOverrideFlag(true);
		map.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
	}
	auto magic = map.getSettings().getValue(EGameSettings::MAGIC_NEW_HORIZONS);
	if(magic.isStruct() && magic["adventureSpells"]["core:waterWalk"].isStruct())
	{
		magic["adventureSpells"]["core:waterWalk"].Struct().erase("requireLegalDayEnd");
		magic.Struct().erase("protectedAdventureBarriers");
		magic.setOverrideFlag(true);
		map.overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
}
