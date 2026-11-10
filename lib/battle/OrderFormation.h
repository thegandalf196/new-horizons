/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "HeroCommandRevision.h"
class JsonNode;
class CGHeroInstance;
namespace orderFormation
{
/// Configurable formation prototype, parsed only from captured heroCommands rules.
/// Efficiency scales the derived rating, never any fixed base component.
DLL_LINKAGE int64_t rating(const JsonNode & rules, int attack, int defense, int64_t command);
DLL_LINKAGE int64_t command(const CGHeroInstance & hero);
DLL_LINKAGE int secondWindPercent(const JsonNode & rules, int attack, int defense, int64_t command, int efficiency,
	bool crownAndAltar = false);
DLL_LINKAGE heroCommands::RevisedOrderSnapshot snapshot(const JsonNode & rules, HeroCommand order,
	int32_t round, int attack, int defense, int64_t command,
	int attackEfficiency, int defenseEfficiency, int formationEfficiency, bool crown = false);
DLL_LINKAGE void validateRules(const JsonNode & rules);
}
