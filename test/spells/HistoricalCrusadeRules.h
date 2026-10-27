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
	// This fixture represents a capture before the later Sorcery revision too.
	// Restore its authored roster/costs, rather than retaining installed defaults.
	auto & spells = rules["spells"].Struct();
	spells.erase("new-horizons:summonLivingArmor");
	if(auto haste = spells.find("core:haste"); haste != spells.end())
		haste->second.Struct().erase("ordinaryAcquisition");
	const auto restore = [&](const char * identity, int level, int lowCost, int highCost, const char * clause)
	{
		const auto found = spells.find(identity);
		if(found == spells.end()) return;
		auto & row = found->second;
		if(clause) row.Struct().erase(clause);
		row["level"].Integer() = level;
		auto & costs = row["costs"].Vector();
		costs.clear();
		for(const int cost : {lowCost, lowCost, highCost, highCost}) costs.emplace_back(cost);
	};
	restore("new-horizons:focusMagic", 3, 11, 11, "focusMagic");
	restore("core:slow", 1, 4, 3, "slow");
	restore("new-horizons:massSlow", 1, 12, 9, nullptr);
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
