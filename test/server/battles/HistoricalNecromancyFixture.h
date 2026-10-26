/*
 * HistoricalNecromancyFixture.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "HistoricalNecromancyFixtureData.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/json/JsonNode.h"

#include <stdexcept>

namespace necromancyTest
{
inline JsonNode historicalRules()
{
	const JsonNode captured(historicalNecromancyCapture, sizeof(historicalNecromancyCapture) - 1,
		"HistoricalNecromancyFixtureData");
	if(captured["sourceRevision"].String() != "a08ffecbaec569516cbdd1067d73139ca7316e9d"
		|| captured["skill"]["id"].String() != "new-horizons:necromancy")
		throw std::runtime_error("Historical Necromancy fixture identity mismatch");
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	// Preserve the entire old Skill, including names, tiers and effect metadata.
	// Removing only the policy would leave the new roster in a historical test.
	rules["skills"]["new-horizons:necromancy"] = captured["skill"];
	rules.Struct().erase("necromancyPolicy");
	rules.setOverrideFlag(true);
	return rules;
}
}
