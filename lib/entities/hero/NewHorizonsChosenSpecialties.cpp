/*
	* Part of VCMI / New Horizons.
	* License: GNU General Public License v2.0 or later; see license.txt
	*/
#include "StdInc.h"
#include "NewHorizonsChosenSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "NewHorizonsRuleValidation.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsSpellAvailability.h"
#include "../../spells/CSpell.h"

namespace newHorizonsHeroes
{
int chosenSpecialtyAdjustedCost(int cost, int discountPercent)
{
	detail::require(cost >= 0 && discountPercent >= 0 && discountPercent <= 100, "Chosen specialty cost");
	if(discountPercent == 0)
		return cost;
	return static_cast<int>(std::max<int64_t>(1, (static_cast<int64_t>(cost) * (100 - discountPercent) + 99) / 100));
}

bool resolveChosenSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, SpecialtyPresentation & result)
{
	const auto rules = chosenSpecialtyRules(development);
	const auto & key = hero.getJsonKey();
	if(!rules || (key != "core:inteus" && key != "core:loynis")
		|| !newHorizonsMagic::rulesActive(magic))
		return false;
	const SpellID chosen(SpellID::decode("new-horizons:theChosen"));
	if(!chosen.hasValue() || !newHorizonsMagic::spellAllowedByHeroRoster(magic, chosen))
		return false;
	result.spell = chosen;
	if(key == "core:inteus")
	{
		result.name = "Fervor";
		result.artwork = "newHorizons/heroSpecialties/InteusCrusade";
		result.description = "The Chosen lasts +" + std::to_string(rules->inteusDurationBonus)
			+ " rounds after its ordinary duration cap. Other duration extensions also apply. Its granted abilities are unchanged.";
	}
	else
	{
		result.name = "Anointed Champions";
		result.artwork = "newHorizons/heroSpecialties/LoynisCrusade";
		result.description = "The Chosen costs " + std::to_string(rules->loynisManaDiscountPercent)
			+ "% less Mana after Wisdom, rounded up (minimum 1). Other cost modifiers still apply. Its granted abilities are unchanged.";
	}
	return true;
}

void validateChosenSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("chosenSpecialties"))
		return;
	const auto & row = development["chosenSpecialties"];
	detail::fields(row, {"version", "inteusDurationBonus", "loynisManaDiscountPercent"});
	detail::require(detail::integer(row["version"], 1, 1), "Chosen specialty version");
	detail::require(detail::integer(row["inteusDurationBonus"], 0, 100), "Inteus duration bonus");
	detail::require(detail::integer(row["loynisManaDiscountPercent"], 0, 100), "Loynis mana discount");
	if(!supported)
		throw std::runtime_error("Captured Chosen specialties require the new save format");
}

std::optional<ChosenSpecialtyRules> chosenSpecialtyRules(const JsonNode & development)
{
	validateChosenSpecialtiesSerialization(development, true);
	if(!usesRules(development) || !development.Struct().contains("chosenSpecialties"))
		return std::nullopt;
	const auto & row = development["chosenSpecialties"];
	return ChosenSpecialtyRules{static_cast<int>(row["inteusDurationBonus"].Integer()),
		static_cast<int>(row["loynisManaDiscountPercent"].Integer())};
}

int chosenSpecialtyDurationBonus(const JsonNode & development, std::string_view heroKey, SpellID spell)
{
	const auto rules = chosenSpecialtyRules(development);
	return rules && heroKey == "core:inteus" && spell.hasValue()
		&& spell.toSpell()->getJsonKey() == "new-horizons:theChosen" ? rules->inteusDurationBonus : 0;
}

int chosenSpecialtyManaDiscount(const JsonNode & development, std::string_view heroKey, SpellID spell)
{
	const auto rules = chosenSpecialtyRules(development);
	return rules && heroKey == "core:loynis" && spell.hasValue()
		&& spell.toSpell()->getJsonKey() == "new-horizons:theChosen" ? rules->loynisManaDiscountPercent : 0;
}

std::optional<SpellID> chosenStartingSpellReplacement(const JsonNode & development,
	const JsonNode & magic, std::string_view heroKey, SpellID source)
{
	if(!chosenSpecialtyRules(development)
		|| !((heroKey == "core:inteus" && source == SpellID::BLOODLUST)
			|| (heroKey == "core:loynis" && source == SpellID::PRAYER))
		|| !newHorizonsMagic::rulesActive(magic))
		return std::nullopt;
	const SpellID target(SpellID::decode("new-horizons:theChosen"));
	if(target.hasValue() && newHorizonsMagic::spellAllowedByHeroRoster(magic, target))
		return target;
	return std::nullopt;
}
}
