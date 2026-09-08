/*
 * NewHorizonsMagicProfileFixture.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <cstdlib>
#include <stdexcept>

namespace newHorizonsTest
{
inline bool managedMissileProfileRequested()
{
	const auto * value = std::getenv("NH_REQUIRE_MANAGED_MISSILE_PROFILE");
	return value && std::string(value) == "1";
}

// Replace, rather than deep-merge over installed70, before initializing a world.
// Registry definitions and module identity remain installed and unchanged.
class MagicV1Baseline
{
	std::unique_ptr<GameSettings> prior;
public:
	MagicV1Baseline()
	{
		const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(rules["rulesetVersion"].Integer() != 1 || rules["spells"].Struct().size() != 69)
			throw std::runtime_error("Fixture requires unchanged canonical v1/69 magic baseline");
		auto full = LIBRARY->settingsHandler->getFullConfig();
		full["magic"]["newHorizons"] = rules;
		auto replacement = std::make_unique<GameSettings>();
		replacement->loadBase(full);
		prior = std::move(LIBRARY->settingsHandler);
		LIBRARY->settingsHandler = std::move(replacement);
	}
	~MagicV1Baseline() { LIBRARY->settingsHandler = std::move(prior); }
	MagicV1Baseline(const MagicV1Baseline &) = delete;
	MagicV1Baseline & operator=(const MagicV1Baseline &) = delete;
};
}
