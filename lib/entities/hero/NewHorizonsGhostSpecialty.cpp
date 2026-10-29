/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsGhostSpecialty.h"
#include "../../battle/NewHorizonsSoulHarvest.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateGhostSpecialtySerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("ghostSpecialty"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Ghost specialty requires the new save format");
	const auto & row = rules["ghostSpecialty"];
	detail::fields(row, {"version"});
	detail::require(detail::integer(row["version"], 1, 1), "Ghost specialty version");
	detail::require(!rules.Struct().contains("reclamationSpecialty"), "Vidomina specialty identities are exclusive");
}
bool usesGhostUpgrade(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules.isStruct() || !rules.Struct().contains("ghostSpecialty"))
		return false;
	validateGhostSpecialtySerialization(rules, true);
	return true;
}
bool hasGhostUpgrade(const CGHeroInstance & hero)
{
	return hero.getHeroTypeID().hasValue()
		&& hero.getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:vidomina"))
		&& usesGhostUpgrade(hero.getPrimaryGrowthRules());
}
bool resolveGhostSpecialtyPresentation(const CHero & hero, const JsonNode & rules, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:vidomina" || !usesGhostUpgrade(rules))
		return false;
	result.name = "Soul Harvest";
	result.artwork = "newHorizons/heroSpecialties/VidominaSoulHarvest";
	result.creature = CreatureID(CreatureID::decode("new-horizons:ghost"));
	result.description = "Vidomina may upgrade Wraiths to Ghosts at the ordinary upgrade cost. "
		"Ghosts retain Wraith combat statistics. Each enemy creature personally killed by a Ghost stack "
		"grants it " + std::to_string(newHorizonsSoulHarvest::GHOSTS_PER_CREATURE_KILLED)
		+ " extra Ghost for this battle only. Partial casualties count, and creatures killed again after resurrection count again; "
		"clones and summoned creatures do not count. Extra Ghosts never join the world army.";
	return true;
}
}
