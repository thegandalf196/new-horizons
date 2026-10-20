/*
 * NewHorizonsTravelDayEnd.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsMagic.h"
#include <stdexcept>

namespace newHorizonsMagic
{
namespace
{
void validatePolicy(const JsonNode & rules, const char * spell, bool supported)
{
	const auto & row = rules["adventureSpells"][spell];
	if(!row.isStruct() || !row.Struct().contains("requireLegalDayEnd"))
		return;
	if(!supported || !row["requireLegalDayEnd"].isBool())
		throw std::runtime_error("Unsupported or invalid captured travel day-end policy");
	if(std::string_view(spell) == "core:fly"
		&& (!rules["rulesetVersion"].isNumber() || rules["rulesetVersion"].Float() != CURRENT_RULESET_VERSION))
		throw std::runtime_error("Fly day-end policy requires the current captured magic rules");
}
bool enabled(const JsonNode & rules, const char * spell)
{
	return rulesActive(rules)
		&& rules["rulesetVersion"].Integer() == CURRENT_RULESET_VERSION
		&& rules["adventureSpells"][spell]["requireLegalDayEnd"].isBool()
		&& rules["adventureSpells"][spell]["requireLegalDayEnd"].Bool();
}
}

void validateWaterWalkDayEndSerialization(const JsonNode & rules, bool supported)
{
	const auto & row = rules["adventureSpells"]["core:waterWalk"];
	if(!row.isStruct() || !row.Struct().contains("requireLegalDayEnd"))
		return;
	if(!supported)
		throw std::runtime_error("Water Walk day-end policy requires the new save format");
	if(!row["requireLegalDayEnd"].isBool())
		throw std::runtime_error("Water Walk requireLegalDayEnd must be boolean");
}
void validateTravelDayEndSerialization(const JsonNode & rules, bool waterWalkSupported, bool flySupported)
{
	validateWaterWalkDayEndSerialization(rules, waterWalkSupported);
	validatePolicy(rules, "core:fly", flySupported);
}
bool requiresWaterWalkLegalDayEnd(const JsonNode & rules)
{
	validateWaterWalkDayEndSerialization(rules, true);
	return rulesActive(rules)
		&& rules["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& rules["adventureSpells"]["core:waterWalk"]["requireLegalDayEnd"].isBool()
		&& rules["adventureSpells"]["core:waterWalk"]["requireLegalDayEnd"].Bool();
}
bool requiresFlyLegalDayEnd(const JsonNode & rules)
{
	validatePolicy(rules, "core:fly", true);
	return enabled(rules, "core:fly");
}
}
