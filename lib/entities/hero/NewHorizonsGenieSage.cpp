/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsGenieSage.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include <limits>
namespace newHorizonsHeroes
{
void validateGenieSageSerialization(const JsonNode & rules, bool supported)
{
	if(!rules.isStruct() || !rules.Struct().contains("genieSageSpecialties"))
		return;
	if(!supported)
		throw std::runtime_error("Captured Genie and Sage specialties require their save capability");
	const auto & row = rules["genieSageSpecialties"];
	detail::fields(row, {"version"});
	detail::require(detail::integer(row["version"], 1, 1), "Genie and Sage specialty version");
}
bool usesGenieSage(const JsonNode & rules)
{
	if(!rules.isStruct() || !usesRules(rules) || !rules.Struct().contains("genieSageSpecialties"))
		return false;
	validateGenieSageSerialization(rules, true);
	return true;
}
bool hasSageOfTheAges(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:thane"))
		&& usesGenieSage(hero->getPrimaryGrowthRules());
}
bool hasWheelOfFortune(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:iona"))
		&& usesGenieSage(hero->getPrimaryGrowthRules());
}
std::array<int, GameConstants::PRIMARY_SKILLS> sageGrowth(const CGHeroInstance & hero)
{
	return hasSageOfTheAges(&hero) ? std::array<int, GameConstants::PRIMARY_SKILLS>{0, 0, 1, 1}
		: std::array<int, GameConstants::PRIMARY_SKILLS>{};
}
std::array<int, GameConstants::PRIMARY_SKILLS> completeSageGrowth(const CGHeroInstance & hero,
	std::array<int, GameConstants::PRIMARY_SKILLS> gains)
{
	const auto extra = sageGrowth(hero);
	for(size_t i = 0; i < gains.size(); ++i)
	{
		if(gains[i] > std::numeric_limits<int>::max() - extra[i])
			throw std::overflow_error("Sage of the Ages primary growth overflow");
		gains[i] += extra[i];
	}
	return gains;
}
bool resolveGenieSagePresentation(const CHero & hero, const JsonNode & rules,
	SpecialtyPresentation & result)
{
	if(!usesGenieSage(rules))
		return false;
	if(hero.getJsonKey() == "core:thane")
	{
		result.name = "Sage of the Ages";
		result.artwork = "newHorizons/heroSpecialties/ThaneSageOfTheAges";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
		result.description = "Gain +1 Spell Power and +1 Knowledge on each level gained, in addition to normal class and Skill growth. No additional ratings are granted at level 1.";
		return true;
	}
	if(hero.getJsonKey() != "core:iona")
		return false;
	result.name = "Wheel of Fortune";
	result.artwork = "newHorizons/heroSpecialties/IonaWheelOfFortune";
	result.creature = CreatureID(CreatureID::decode("core:genie"));
	result.description = "Genies and Master Genies may cast Bestow Luck on one friendly stack for +3 Luck. Master Genies may also cast Turn of Fate on one enemy stack for -3 Luck. Each lasts 5 rounds and uses one of the stack's three total Wishes charges. These effects replace existing Fortune or Misfortune effects.";
	return true;
}
}
