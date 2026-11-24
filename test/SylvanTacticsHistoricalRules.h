/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../lib/json/JsonNode.h"
#include "../lib/filesystem/ResourcePath.h"

/// Installed perk rules with the Sylvan Tactics roster that policy 1-4 and
/// absent-policy captures keep (the policy-4 installation), so historical
/// tests never see the policy-5 Ambush roster. `policy` 0 removes the marker.
inline JsonNode sylvanTacticsHistoricalRules(int policy)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	const JsonNode historical(JsonPath::builtin("test/newHorizonsSylvanTactics4Skill"));
	rules["skills"]["new-horizons:sylvanLuck"] = historical["skill"];
	if(policy > 0)
		rules["sylvanTacticsPolicy"] = JsonNode(policy);
	else
		rules.Struct().erase("sylvanTacticsPolicy");
	return rules;
}
