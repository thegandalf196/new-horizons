/*
 * HistoricalNecromancy087Fixture.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "HistoricalNecromancy087FixtureData.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/json/JsonNode.h"

#include <stdexcept>

namespace necromancy087Test
{
inline JsonNode rules()
{
	const JsonNode captured(capturedRegistryRows, sizeof(capturedRegistryRows) - 1,
		"HistoricalNecromancy087FixtureData");
	if(captured["sourceRevision"].String() != "51d6dc8eb832db028ec8ddb5a9df38b0e0342c55"
		|| captured["skill"]["id"].String() != "new-horizons:necromancy"
		|| captured["policy"]["version"].Integer() != 1)
		throw std::runtime_error("Historical 0.87 Necromancy fixture identity mismatch");

	auto result = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	// Preserve the complete published Skill and policy, including old meanings.
	result["skills"]["new-horizons:necromancy"] = captured["skill"];
	result["necromancyPolicy"] = captured["policy"];
	result.setOverrideFlag(true);
	return result;
}
}
