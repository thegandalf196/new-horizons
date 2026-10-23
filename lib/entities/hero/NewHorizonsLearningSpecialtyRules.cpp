/*
 * NewHorizonsLearningSpecialtyRules.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLearningSpecialties.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsRuleValidation.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
namespace
{
const char * rowName(LearningSpecialtyKind kind)
{
	return kind == LearningSpecialtyKind::GRAVE_ARCHIVIST ? "graveArchivist" : "spiritTeacher";
}
}
void validateLearningSpecialtyRows(const JsonNode & table)
{
	for(const auto kind : {LearningSpecialtyKind::GRAVE_ARCHIVIST, LearningSpecialtyKind::SPIRIT_TEACHER})
	{
		if(!table.Struct().contains(rowName(kind)))
			continue;
		const auto & row = table[rowName(kind)];
		const bool archivist = kind == LearningSpecialtyKind::GRAVE_ARCHIVIST;
		detail::fields(row, archivist
			? std::initializer_list<std::string_view>{"learningPercentPerLevel", "learningMaximumPercent", "additionalEagleEyeSpells"}
			: std::initializer_list<std::string_view>{"learningPercentPerLevel", "learningMaximumPercent", "mentorExperiencePercent"});
		detail::require(detail::integer(row["learningPercentPerLevel"], 0, 1000), "learning specialty percentage per level");
		detail::require(detail::integer(row["learningMaximumPercent"], 0, 200), "learning specialty maximum percentage");
		detail::require(archivist ? detail::integer(row["additionalEagleEyeSpells"], 0, 10)
			: detail::integer(row["mentorExperiencePercent"], 0, 1000), "learning specialty additional effect");
	}
}
std::optional<LearningSpecialtyRules> learningSpecialtyRules(const JsonNode & development, LearningSpecialtyKind kind)
{
	if(!usesRules(development) || !development.isStruct() || !development.Struct().contains("additionalSpecialties"))
		return std::nullopt;
	validateAdditionalSpecialtiesSerialization(development, true);
	const auto & table = development["additionalSpecialties"];
	if(!table.Struct().contains(rowName(kind)))
		return std::nullopt;
	const auto & row = table[rowName(kind)];
	LearningSpecialtyRules result;
	result.learning = {static_cast<int>(row["learningPercentPerLevel"].Integer()),
		static_cast<int>(row["learningMaximumPercent"].Integer())};
	if(kind == LearningSpecialtyKind::GRAVE_ARCHIVIST)
		result.additionalEagleEyeSpells = static_cast<int>(row["additionalEagleEyeSpells"].Integer());
	else
		result.mentorExperiencePercent = static_cast<int>(row["mentorExperiencePercent"].Integer());
	return result;
}
std::optional<LearningSpecialtyRules> learningSpecialtyRules(const CGHeroInstance * hero)
{
	if(!hero || !hero->getHeroTypeID().hasValue())
		return std::nullopt;
	if(hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:nimbus")))
		return learningSpecialtyRules(hero->getPrimaryGrowthRules(), LearningSpecialtyKind::GRAVE_ARCHIVIST);
	if(hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:oris")))
		return learningSpecialtyRules(hero->getPrimaryGrowthRules(), LearningSpecialtyKind::SPIRIT_TEACHER);
	return std::nullopt;
}
int additionalEagleEyeSpells(const CGHeroInstance * hero)
{
	const auto rules = learningSpecialtyRules(hero);
	return rules ? rules->additionalEagleEyeSpells : 0;
}
int64_t mentorSpecialtyAward(const CGHeroInstance * mentor, int64_t ordinaryAward)
{
	const auto rules = learningSpecialtyRules(mentor);
	if(!rules || ordinaryAward <= 0)
		return ordinaryAward;
	const auto extra = druidSpecialtyExtra(ordinaryAward, rules->mentorExperiencePercent);
	return ordinaryAward > std::numeric_limits<int64_t>::max() - extra
		? std::numeric_limits<int64_t>::max() : ordinaryAward + extra;
}
}
