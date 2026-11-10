/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsCommandState.h"
#include "../../battle/NewHorizonsWarfare.h"

namespace newHorizonsHeroes
{
DLL_LINKAGE void validateCommandPerkSerialization(const JsonNode & perks, bool supported);
DLL_LINKAGE void validateCommandCaptures(const JsonNode & capabilities, const JsonNode & perks,
	const JsonNode & combat, bool supported);
DLL_LINKAGE void validateCommandOverrides(const JsonNode & overrides, bool supported);
DLL_LINKAGE void validateCommandCrossover(const JsonNode & hero, bool supported);
}
