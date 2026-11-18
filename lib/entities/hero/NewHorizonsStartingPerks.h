/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsHeroRules.h"

namespace newHorizonsHeroes
{
/// Optional captured creation policy; absence retains historical initial choices.
DLL_LINKAGE bool usesAuthoredStartingPerks(const JsonNode & development);
/// Versions two through four constrain fresh defaults to two Basic Skills.
DLL_LINKAGE bool usesBasicStartingProfiles(const JsonNode & startingSkills);
/// Version three grants no starting perks, including prototype/default fallbacks.
DLL_LINKAGE bool usesSkillsOnlyStartingProfiles(const JsonNode & startingSkills);
/// Version four grants one authored Basic perk and a spellbook at creation only.
DLL_LINKAGE bool usesSingleStartingPerkProfiles(const JsonNode & startingSkills);
DLL_LINKAGE void validateStartingPerkProfiles(const JsonNode & startingSkills);
}
