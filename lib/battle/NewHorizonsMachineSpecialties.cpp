/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMachineSpecialties.h"
#include "Unit.h"
#include "../json/JsonNode.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/hero/CHero.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace newHorizonsMachineSpecialties
{
namespace
{
int coefficient(const CGHeroInstance * hero, std::string_view identity, const char * key)
{
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != identity
		|| !newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero))
		return 0;
	return static_cast<int>(hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer());
}
}
int masterGunnerFollowUpDamagePercent(const CGHeroInstance * hero, int ordinaryPercent)
{
	if(ordinaryPercent < 0 || ordinaryPercent > 100)
		throw std::runtime_error("Invalid ordinary Master Gunner percentage");
	if(ordinaryPercent == 0)
		return 0;
	const int bonus = coefficient(hero, "core:torosar", "gunneryProfessorFollowUpBonusPoints");
	if(bonus == 0 || !hero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"))
		return ordinaryPercent;
	return std::min(100, ordinaryPercent + bonus);
}
int siegeOutput(const CGHeroInstance & hero, const JsonNode & rules, int siegeRating, const std::string & output)
{
	const int ordinary = newHorizonsHeroes::capabilitySiegeOutput(rules, siegeRating, output);
	if(output != "ballistaDamage" && output != "catapultStructuralDamage")
		return ordinary;
	const int bonus = coefficient(&hero, "core:gurnisson", "heavyBatterySiegeTermBonusPercent");
	if(bonus == 0)
		return ordinary;
	const auto & formula = rules["siege"]["outputs"][output];
	const int64_t term = formula["siegeCoefficientHalf"].Integer() * siegeRating / 2;
	const int64_t enhanced = formula["base"].Integer() + term + term * bonus / 100;
	return static_cast<int>(std::clamp<int64_t>(enhanced, 0, std::numeric_limits<int32_t>::max()));
}
int catapultStructuralOutput(const CGHeroInstance & hero, const battle::Unit & actor, int siegeRating, int resolvedOutput)
{
	if(coefficient(&hero, "core:gurnisson", "heavyBatterySiegeTermBonusPercent") == 0
		|| (actor.unitType() && actor.isCatapult()))
		return resolvedOutput;
	// The old callback also serves Cyclops and identityless legacy adapters.
	return newHorizonsHeroes::capabilitySiegeOutput(hero.getCapabilityRules(), siegeRating, "catapultStructuralDamage");
}

}
