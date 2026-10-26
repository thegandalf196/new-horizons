/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../json/JsonNode.h"
#include "../../constants/EntityIdentifiers.h"
#include <optional>
#include <string_view>

class CHero;

namespace newHorizonsHeroes
{
struct ChosenSpecialtyRules
{
	int inteusDurationBonus;
	int loynisManaDiscountPercent;
};
DLL_LINKAGE int chosenSpecialtyAdjustedCost(int cost, int discountPercent);
DLL_LINKAGE bool resolveChosenSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, struct SpecialtyPresentation & result);
DLL_LINKAGE void validateChosenSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE std::optional<ChosenSpecialtyRules> chosenSpecialtyRules(const JsonNode & development);
DLL_LINKAGE int chosenSpecialtyDurationBonus(const JsonNode & development, std::string_view heroKey, SpellID spell);
DLL_LINKAGE int chosenSpecialtyManaDiscount(const JsonNode & development, std::string_view heroKey, SpellID spell);
DLL_LINKAGE std::optional<SpellID> chosenStartingSpellReplacement(const JsonNode & development,
	const JsonNode & magic, std::string_view heroKey, SpellID source);
}
