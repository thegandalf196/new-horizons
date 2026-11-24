/*
 * OverkillRandomPrayerRules.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../lib/filesystem/ResourcePath.h"
#include "../lib/json/JsonNode.h"
#include "ActivatedOrdinarySpecialties.h"
#include "SylvanTacticsHistoricalRules.h"

/// Pin Metamagic 5 (Overkill, policy 6) and Consecrated Arms 3 (random prayers,
/// policy 3): the shipped rules captured before Double Cast and Chosen Prayers.
/// Saves keep these captured documents, so their executable paths stay covered.
inline JsonNode overkillRandomPrayerRules(JsonNode rules)
{
	const JsonNode pinned(JsonPath::builtin("test/newHorizonsOverkillRandomPrayerRules"));
	for(const auto * skill : {"new-horizons:metamagic", "new-horizons:divineMandate"})
		rules["skills"][skill] = pinned["skills"][skill];
	for(const auto * policy : {"metamagicPolicy", "divineMandatePolicy"})
		rules[policy] = pinned[policy];
	// Those captures also predate core Stewardship and Spellcraft policy 2 (format 1166).
	rules.Struct().erase("spellcraftPolicyVersion");
	if(rules.Struct().contains("estatesPolicyVersion"))
		rules["estatesPolicyVersion"].Integer() = 1;
	// ... and Sylvan Tactics policy 5 (format 1167): keep the policy-4 roster.
	if(rules.Struct().contains("sylvanTacticsPolicy"))
	{
		rules["skills"]["new-horizons:sylvanLuck"] = sylvanTacticsHistoricalRules(4)["skills"]["new-horizons:sylvanLuck"];
		rules["sylvanTacticsPolicy"].Integer() = 4;
	}
	// A complete captured registry: never merge the pinned policies into installed ones.
	rules.setOverrideFlag(true);
	return rules;
}

inline JsonNode overkillRandomPrayerRules()
{
	return overkillRandomPrayerRules(JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
}

/// Installed hero rules whose authored starts name only perks of the pinned
/// roster: Halon's Formula Reserve start returns to Unraveling Magic.
inline JsonNode overkillRandomPrayerHeroes(JsonNode heroes)
{
	for(auto & entry : heroes["startingSkills"]["startingPerkProfiles"]["core:halon"].Vector())
		if(entry["perk"].String() == "new-horizons:metamagic.formulaRecovery")
			entry["perk"].String() = "new-horizons:metamagic.unravelingMagic";
	// Theodorus's Mana Siphon start (Metamagic 6) returns to his earlier Scholar start.
	for(auto & entry : heroes["startingSkills"]["startingPerkProfiles"]["core:theodorus"].Vector())
		if(entry["perk"].String() == "new-horizons:metamagic.manaSiphon")
		{
			entry["skill"].String() = "new-horizons:learning";
			entry["perk"].String() = "new-horizons:learning.scholar";
		}
	heroes["startingSkills"]["startingBookReplacements"].Struct().erase("core:solmyr"); // Added with the Academy starts.
	heroes.Struct().erase("revisedSpecialties"); // Revised specialties require format 1166.
	omitActivatedOrdinaryCoefficients(heroes); // Nor do they carry the 0.93.1 ordinary coefficients.
	heroes.setOverrideFlag(true);
	return heroes;
}

inline JsonNode overkillRandomPrayerHeroes()
{
	return overkillRandomPrayerHeroes(JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
}
