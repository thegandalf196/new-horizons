/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLivingInferno.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateLivingInfernoSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("livingInfernoSpecialty"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Living Inferno requires its save capability");
	const auto & row = rules["livingInfernoSpecialty"];
	detail::fields(row, {"version", "damagePerCreature", "durationRounds"});
	detail::require(detail::integer(row["version"], 1, 1), "Living Inferno version");
	detail::require(detail::integer(row["damagePerCreature"], 0, 1000), "Living Inferno damage per creature");
	detail::require(detail::integer(row["durationRounds"], 1, 1000), "Living Inferno duration");
}
std::optional<LivingInfernoRules> livingInfernoRules(const JsonNode & rules)
{
	if(!usesRules(rules) || !rules.isStruct() || !rules.Struct().contains("livingInfernoSpecialty"))
		return std::nullopt;
	validateLivingInfernoSerialization(rules, true);
	const auto & row = rules["livingInfernoSpecialty"];
	return LivingInfernoRules{static_cast<int>(row["damagePerCreature"].Integer()),
		static_cast<int>(row["durationRounds"].Integer())};
}
bool hasLivingInferno(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:fiur"))
		&& livingInfernoRules(hero->getPrimaryGrowthRules()).has_value();
}
bool resolveLivingInfernoPresentation(const CHero & hero,
	const JsonNode & rules, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:fiur")
		return false;
	const auto policy = livingInfernoRules(rules);
	if(!policy)
		return false;
	result.name = "Living Inferno";
	result.artwork = "newHorizons/heroSpecialties/FiurLivingInferno";
	result.creature = CreatureID(CreatureID::decode("core:fireElemental"));
	result.description = "Fire and Energy Elementals leave fire on vacated hexes along voluntary movement, "
		"including flight. Each trail deals " + std::to_string(policy->damagePerCreature)
		+ " Fire damage per creature present when movement begins, using Fire Wall's normal immunities "
		"and reductions. It lasts " + std::to_string(policy->durationRounds)
		+ " round(s), including this round, and can burn allies. Existing Fire Wall hexes do not stack. "
		"Forced movement, teleportation, deployment and return-after-attack movement leave no trail.";
	return true;
}
}
