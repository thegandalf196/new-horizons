/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsDreadCavalry.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateDreadCavalrySerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("dreadCavalrySpecialty"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Dread Cavalry requires its save capability");
	const auto & row = rules["dreadCavalrySpecialty"];
	detail::fields(row, {"version", "followupDamagePercent"});
	detail::require(detail::integer(row["version"], 1, 1), "Dread Cavalry version");
	detail::require(detail::integer(row["followupDamagePercent"], 1, 100), "Dread Cavalry follow-up output");
}
std::optional<int> dreadCavalryDamagePercent(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules.isStruct() || !rules.Struct().contains("dreadCavalrySpecialty"))
		return std::nullopt;
	validateDreadCavalrySerialization(rules, true);
	return static_cast<int>(rules["dreadCavalrySpecialty"]["followupDamagePercent"].Integer());
}
bool hasDreadCavalry(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:tamika"))
		&& dreadCavalryDamagePercent(hero->getPrimaryGrowthRules()).has_value();
}
bool resolveDreadCavalryPresentation(const CHero & hero,
	const JsonNode & rules, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:tamika")
		return false;
	const auto percent = dreadCavalryDamagePercent(rules);
	if(!percent)
		return false;
	result.name = "Dread Cavalry";
	result.artwork = "newHorizons/heroSpecialties/TamikaDreadCavalry";
	result.creature = CreatureID(CreatureID::decode("core:blackKnight"));
	result.description = "Once per Creature Activation, Black and Dread Knights that destroy an enemy "
		"during their own melee attack may attack a different enemy at " + std::to_string(*percent)
		+ "% damage, moving with their remaining movement. The normal attack sequence, Luck, Death Blow "
		"and enemy retaliation apply. This grants no new activation, movement refill, Charge ability, "
		"Hero Action or Mana. The charge may be declined; separately earned Pursuit movement remains available.";
	return true;
}
}
