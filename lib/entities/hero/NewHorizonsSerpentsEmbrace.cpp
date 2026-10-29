/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateSerpentsEmbraceSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("serpentsEmbrace"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Serpent's Embrace requires its source-activation save capability");
	validateAdditionalSpecialtiesSerialization(rules, true);
}
std::optional<bool> serpentsEmbracePolicy(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules.Struct().contains("additionalSpecialties")
		|| !rules["additionalSpecialties"].isStruct()
		|| !rules["additionalSpecialties"].Struct().contains("serpentsEmbrace"))
		return std::nullopt;
	validateSerpentsEmbraceSerialization(rules, true);
	return rules["additionalSpecialties"]["serpentsEmbrace"]["enabled"].Bool();
}
bool hasSerpentsEmbrace(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:fafner"))
		&& serpentsEmbracePolicy(hero->getPrimaryGrowthRules()).has_value();
}
bool serpentsEmbraceEnabled(const CGHeroInstance * hero)
{
	return hasSerpentsEmbrace(hero) && serpentsEmbracePolicy(hero->getPrimaryGrowthRules()).value();
}
}
