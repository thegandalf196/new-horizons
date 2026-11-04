/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../HistoricalFactionPerkRules.h"
#include "../../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/battle/BattleInfo.h"

namespace historicalBloodrageFixture
{
/// The old Bloodrage codecs precede modern Hero, spell and combat-policy captures.
/// Preserve Bloodrage definitions and selected receipts, with a coherent empty
/// Hero development context and the existing unweighted legacy offer policy.
inline void captureContext(CMap & map)
{
	auto perks = historicalFactionPerkRules(
		map.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS));
	for(auto & perk : perks["skills"]["new-horizons:command"]["perks"].Vector())
		if(perk["id"].String() == "new-horizons:command.crisisCommand")
			perk["effect"]["status"].String() = "planned";
	map.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	map.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
	captureLegacyPerkOfferPolicy(map);
	map.overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, JsonNode());
	map.overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	map.overrideGameSetting(EGameSettings::COMBAT_NEW_HORIZONS_FINAL_LUCK, JsonNode(false));
	map.overrideGameSetting(EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT, JsonNode(100));
	isolateHistoricalAdventurePolicies(map);
}

/// Army Value was introduced after the original counter/cap/threshold codecs.
/// Clear only that later metadata when authoring their absent-field control.
inline void capturePreArmyValueSnapshot(BattleInfo & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		battle.getSide(side).initialArmyValue.reset();
		battle.getSide(side).initialArmyIsWandering = false;
	}
}
}
