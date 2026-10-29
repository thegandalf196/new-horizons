/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPetrifyingBlood.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../GameLibrary.h"
#include "../../texts/CGeneralTextHandler.h"
namespace newHorizonsHeroes
{
namespace
{
bool captured(const JsonNode & rules)
{
	return usesRules(rules) && rules.isStruct() && rules.Struct().contains("petrifyingBloodSpecialty");
}
}
void validatePetrifyingBloodSerialization(const JsonNode & rules, bool supported)
{
	if(!captured(rules))
		return;
	if(!supported)
		throw std::runtime_error("Captured Petrifying Blood requires the new save format");
	const auto & row = rules["petrifyingBloodSpecialty"];
	detail::fields(row, {"version"});
	detail::require(detail::integer(row["version"], 1, 1), "Petrifying Blood version");
}
bool hasPetrifyingBlood(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:bron"))
		&& captured(hero->getPrimaryGrowthRules());
}
bool resolvePetrifyingBloodPresentation(const CHero & hero, const JsonNode & rules,
	SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:bron" || !captured(rules))
		return false;
	validatePetrifyingBloodSerialization(rules, true);
	result.name = LIBRARY->generaltexth->translate("new-horizons.heroSpecialty.petrifyingBlood.name");
	result.description = LIBRARY->generaltexth->translate("new-horizons.heroSpecialty.petrifyingBlood.description");
	result.creature = CreatureID(CreatureID::decode("core:basilisk"));
	result.artwork = "newHorizons/heroSpecialties/BronPetrifyingBlood";
	return true;
}
}
