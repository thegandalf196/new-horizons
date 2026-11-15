/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsHeroRules.h"

namespace newHorizonsHeroes
{
/// Optional captured creation policy; absence retains historical initial choices.
DLL_LINKAGE bool usesAuthoredStartingPerks(const JsonNode & development);
/// Version two constrains fresh defaults only; historical captures keep their ranks.
DLL_LINKAGE bool usesBasicStartingProfiles(const JsonNode & startingSkills);
DLL_LINKAGE void validateStartingPerkProfiles(const JsonNode & startingSkills);
}
