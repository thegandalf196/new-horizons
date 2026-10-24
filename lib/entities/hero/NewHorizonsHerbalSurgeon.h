/*
 * NewHorizonsHerbalSurgeon.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
#include <optional>
class JsonNode;
class CHero;
class CGHeroInstance;
namespace spells { class IBattleCast; }
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
struct DLL_LINKAGE HerbalSurgeonRules
{
	int tentPercentPerLevel = 0;
	int tentMaximumPercent = 0;
	int regenerationPercentPerLevel = 0;
	int regenerationMaximumPercent = 0;
};
DLL_LINKAGE void validateHerbalSurgeonRow(const JsonNode & table);
DLL_LINKAGE std::optional<HerbalSurgeonRules> herbalSurgeonRules(const JsonNode & development);
DLL_LINKAGE int herbalSurgeonRegenerationPercent(const CGHeroInstance * hero);
DLL_LINKAGE int64_t tentCreatureHealingBudget(int64_t ordinary, const CGHeroInstance * hero);
/// Explicit zero belongs to the original event and must not be recaptured.
DLL_LINKAGE int resolveHerbalSurgeonRegenerationPercent(const spells::IBattleCast * event);
DLL_LINKAGE bool resolveHerbalSurgeonPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result);
}
