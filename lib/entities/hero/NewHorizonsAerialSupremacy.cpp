/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsAerialSupremacy.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateAerialSupremacySerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("aerialSupremacy"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Aerial Supremacy requires its aerial retaliation save capability");
	validateAdditionalSpecialtiesSerialization(rules, true);
}
std::optional<bool> aerialSupremacyPolicy(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("aerialSupremacy"))
		return std::nullopt;
	validateAerialSupremacySerialization(rules, true);
	return rules["additionalSpecialties"]["aerialSupremacy"]["enabled"].Bool();
}
bool hasAerialSupremacy(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:edric"))
		&& aerialSupremacyPolicy(hero->getPrimaryGrowthRules()).has_value();
}
bool aerialSupremacyEnabled(const CGHeroInstance * hero)
{
	return hasAerialSupremacy(hero) && aerialSupremacyPolicy(hero->getPrimaryGrowthRules()).value();
}
bool resolveAerialSupremacyPresentation(const CHero & hero, const JsonNode & rules,
	SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:edric")
		return false;
	const auto enabled = aerialSupremacyPolicy(rules);
	if(!enabled)
		return false;
	result.name = "Aerial Supremacy";
	result.creature = CreatureID(CreatureID::decode("core:griffin"));
	if(*enabled)
		result.artwork = "newHorizons/heroSpecialties/EdricAerialSupremacy";
	result.description = *enabled
		? "Griffins answer eligible ranged attacks within movement range with a flying melee counter, then return. Each uses an ordinary retaliation. Griffins can do this once per round; Royal Griffins have no additional aerial limit. Their original battlefield position remains occupied throughout."
		: "Aerial Supremacy is disabled for this game.";
	return true;
}

}
