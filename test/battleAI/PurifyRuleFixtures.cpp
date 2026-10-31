/*
 * PurifyRuleFixtures.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PurifyRuleFixtures.h"
#include "../spells/HistoricalCrusadeRules.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace purifyTest
{
JsonNode savedV2MagicRules()
{
	// Reuse the complete historical roster; current Sorcery clauses are not v2 data.
	auto rules = historicalCrusadeRules(JsonNode(JsonPath::builtin("config/newHorizonsMagic")),
		newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	newHorizonsMagic::validateRules(rules);
	return rules;
}

JsonNode neutralSpellCompetitionCommands()
{
	const JsonNode combatRules(JsonPath::builtin("config/newHorizonsCombat"));
	JsonNode commands = combatRules["combat"]["heroCommands"];
	for(auto & command : commands["commands"].Struct())
		for(auto & effect : command.second["effects"].Struct())
		{
			// Preserve each current formula's keys; revised Orders have no attack/defense fields.
			for(const auto * field : {"base", "attack", "defense", "leadership", "minimumRating"})
				if(effect.second.Struct().contains(field))
					effect.second[field].Float() = 0;
			// Vanguard's ratingStep must stay positive; canonical finalization clamps to this cap.
			if(effect.second.Struct().contains("cap"))
				effect.second["cap"].Integer() = 0;
		}
	heroCommands::validateRules(commands);
	return commands;
}
}
