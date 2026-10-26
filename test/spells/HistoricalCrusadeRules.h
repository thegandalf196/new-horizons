/* Historical captured Light roster for compatibility characterizations.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/json/JsonNode.h"

inline JsonNode historicalCrusadeRules(JsonNode rules, int version = 3)
{
	// A loaded pre-revision game keeps Crusade and never acquires The Chosen.
	rules["spells"].Struct().erase("new-horizons:theChosen");
	rules["spells"]["new-horizons:crusade"].Struct().erase("active");
	rules["rulesetVersion"].Integer() = version;
	if(version < 3)
	{
		// These clauses did not exist in the requested captured policy. Keep the
		// production validator strict instead of disguising modern rules as v1/v2.
		for(const auto * key : {"schoolRankPowerCoefficientPercent", "spellcraftEfficiencyPercent",
			"morale", "creatureAbilities", "protectedAdventureBarriers", "heroLuck", "heroMorale"})
			rules.Struct().erase(key);
		for(auto & [name, row] : rules["spells"].Struct())
		{
			(void)name;
			const bool laterVariant = row.Struct().contains("variant");
			for(const auto * key : {"heroAccess", "restoration", "propagationLimit", "implosion",
				"ignoreInterveningBarriers", "temporaryMagicalEffectsOnly", "schoolRankDurations", "burnGroundedFlyers",
				"selectedPlacement", "rawSpellPowerPerPatch", "earthquake", "structures", "variant"})
				row.Struct().erase(key);
			if(laterVariant)
				row["active"].Bool() = false;
		}
		for(auto & [name, row] : rules["adventureSpells"].Struct())
		{
			(void)name;
			row.Struct().erase("requireLegalDayEnd");
		}
		if(version == 2)
		{
			auto & sorrow = rules["spells"]["core:sorrow"];
			sorrow.Struct().erase("level");
			sorrow.Struct().erase("costs");
			sorrow["schools"].Vector().clear();
			sorrow["schools"].Vector().emplace_back(std::string("new-horizons:chaos"));
		}
	}
	return rules;
}
