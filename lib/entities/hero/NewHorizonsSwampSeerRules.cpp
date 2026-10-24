/*
 * NewHorizonsSwampSeerRules.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSwampSeer.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateSwampSeerSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("swampSeerSpecialty"))
		return;
	if(!supported)
		throw std::runtime_error("Swamp Seer requires the new save format");
	const auto & rule = development["swampSeerSpecialty"];
	detail::fields(rule, {"version", "learningPercentPerLevel", "learningMaximumPercent", "studyExperiencePerMagicPrimaryPoint"});
	detail::require(detail::integer(rule["version"], 1, 1), "Swamp Seer version");
	detail::require(detail::integer(rule["learningPercentPerLevel"], 0, 1000), "Swamp Seer learning percentage per level");
	detail::require(detail::integer(rule["learningMaximumPercent"], 0, 200), "Swamp Seer learning maximum percentage");
	detail::require(detail::integer(rule["studyExperiencePerMagicPrimaryPoint"], 0, 1000000), "Swamp Seer study Experience");
}
std::optional<SwampSeerRules> swampSeerRules(const JsonNode & development)
{
	validateSwampSeerSerialization(development, true);
	if(!usesRules(development) || !development.Struct().contains("swampSeerSpecialty"))
		return std::nullopt;
	const auto & rule = development["swampSeerSpecialty"];
	return SwampSeerRules{static_cast<int>(rule["learningPercentPerLevel"].Integer()),
		static_cast<int>(rule["learningMaximumPercent"].Integer()),
		static_cast<int>(rule["studyExperiencePerMagicPrimaryPoint"].Integer())};
}
std::optional<SwampSeerRules> swampSeerRules(const CGHeroInstance * hero)
{
	if(!hero)
		return std::nullopt;
	validateSwampSeerSerialization(hero->getPrimaryGrowthRules(), true);
	return hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:tiva"))
		? swampSeerRules(hero->getPrimaryGrowthRules()) : std::nullopt;
}
}
