/*
 * HistoricalFactionPerkRules.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../lib/filesystem/ResourcePath.h"
#include "../lib/json/JsonNode.h"

#include <initializer_list>

/// Pin pre-passive faction skills and original Bloodrage; retain other captured rules.
inline JsonNode historicalFactionPerkRules(JsonNode rules)
{
	const JsonNode historical(JsonPath::builtin("test/newHorizonsHistoricalFactionPerks"));
	for(const auto * skill : {"new-horizons:metamagic", "new-horizons:divineMandate",
		"new-horizons:demonicGating"})
		rules["skills"][skill] = historical["skills"][skill];
	for(const auto * policy : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
		rules.Struct().erase(policy);
	// This is a complete captured registry: absent policies must stay absent
	// when GameSettings applies it over installed current defaults.
	rules.setOverrideFlag(true);
	return rules;
}

/// Installed rules supply all unrelated systems, including current Quick Study.
inline JsonNode historicalFactionPerkRules()
{
	return historicalFactionPerkRules(JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
}
