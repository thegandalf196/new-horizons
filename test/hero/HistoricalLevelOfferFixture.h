/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../server/LegacyEstatesCapture.h"

namespace historicalLevelOffer
{
// The flat Skill/perk envelope predates mandatory rank-plus-perk progression.
// Exercise that captured contract without redefining the installed defaults.
inline JsonNode perks()
{
	auto rules = estatesTest::legacyEstatesPerks();
	rules.Struct().erase("unifiedSkillProgressionVersion");
	rules.setOverrideFlag(true);
	return rules;
}
inline JsonNode heroes()
{
	auto rules = estatesTest::legacyEstatesHeroes();
	rules["startingSkills"].Struct().erase("startingPerksPolicyVersion");
	rules["startingSkills"].Struct().erase("startingPerkProfiles");
	rules.setOverrideFlag(true);
	return rules;
}
}
