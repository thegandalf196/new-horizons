/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsManaLeeches.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateManaLeechesSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("additionalSpecialties"))
		return;
	const auto & table = development["additionalSpecialties"];
	if(!table.isStruct() || !table.Struct().contains("manaLeeches"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Mana Leeches require the new save format");
	validateAdditionalSpecialtiesSerialization(development, true);
}
std::optional<int> manaLeechesAmount(const JsonNode & development)
{
	if(!usesRules(development) || !development.Struct().contains("additionalSpecialties"))
		return std::nullopt;
	const auto & table = development["additionalSpecialties"];
	if(!table.isStruct() || !table.Struct().contains("manaLeeches"))
		return std::nullopt;
	validateManaLeechesSerialization(development, true);
	return static_cast<int>(table["manaLeeches"]["manaPerDamagingAttackSequence"].Integer());
}
bool hasManaLeeches(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:ignatius"))
		&& manaLeechesAmount(hero->getPrimaryGrowthRules()).has_value();
}
}
