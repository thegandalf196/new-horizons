/*
 * PurifyRuleFixtures.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PurifyRuleFixtures.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace purifyTest
{
JsonNode savedV2MagicRules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	// A historical v2 capture must not contain later v3-only policies.
	for(const auto * key : {"protectedAdventureBarriers", "creatureAbilities", "morale", "heroLuck", "heroMorale"})
		rules.Struct().erase(key);
	rules["adventureSpells"]["core:waterWalk"].Struct().erase("requireLegalDayEnd");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("heroAccess");
		spell.Struct().erase("earthquake");
		spell.Struct().erase("structures");
		spell.Struct().erase("restoration");
		for(const auto * key : {"propagationLimit", "implosion", "ignoreInterveningBarriers",
			"temporaryMagicalEffectsOnly", "schoolRankDurations", "burnGroundedFlyers"})
			spell.Struct().erase(key);
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	if(newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
		< newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		auto & sorrow = rules["spells"]["core:sorrow"];
		sorrow.Struct().erase("level");
		sorrow.Struct().erase("costs");
		sorrow["schools"].Vector().clear();
		sorrow["schools"].Vector().emplace_back(std::string("new-horizons:chaos"));
	}
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
			effect.second["base"].Float() = 0;
			effect.second["attack"].Float() = 0;
			effect.second["defense"].Float() = 0;
		}
	heroCommands::validateRules(commands);
	return commands;
}
}
