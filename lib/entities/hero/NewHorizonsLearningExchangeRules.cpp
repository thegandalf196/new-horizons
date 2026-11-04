/*
 * NewHorizonsLearningExchangeRules.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
namespace
{
const char * rowName(LearningExchangeSpecialtyKind kind)
{
	switch(kind)
	{
	case LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR: return "ecumenicalScholar";
	case LearningExchangeSpecialtyKind::OCCULT_SCHOLAR: return "occultScholar";
	}
	throw std::invalid_argument("Invalid learning exchange specialty kind");
}

}
void validateLearningExchangeSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("learningExchangeSpecialties"))
		return;
	if(!supported)
		throw std::runtime_error("Learning exchange specialties require the new save format");
	const auto & table = development["learningExchangeSpecialties"];
	detail::fields(table, {"version", "ecumenicalScholar", "occultScholar"});
	detail::require(detail::integer(table["version"], 1, 1), "learning exchange specialty version");
	for(const auto kind : {LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR, LearningExchangeSpecialtyKind::OCCULT_SCHOLAR})
	{
		if(!table.Struct().contains(rowName(kind)))
			continue;
		const auto & row = table[rowName(kind)];
		detail::fields(row, {"learningPercentPerLevel", "learningMaximumPercent"});
		detail::require(detail::integer(row["learningPercentPerLevel"], 0, 1000), "exchange learning percentage per level");
		detail::require(detail::integer(row["learningMaximumPercent"], 0, 200), "exchange learning maximum percentage");
	}
}
std::optional<LearningExchangeSpecialtyRules> learningExchangeSpecialtyRules(
	const JsonNode & development, LearningExchangeSpecialtyKind kind)
{
	validateLearningExchangeSpecialtiesSerialization(development, true);
	if(!usesRules(development) || !development["learningExchangeSpecialties"].isStruct())
		return std::nullopt;
	const auto & table = development["learningExchangeSpecialties"];
	if(!table.Struct().contains(rowName(kind)))
		return std::nullopt;
	const auto & row = table[rowName(kind)];
	LearningExchangeSpecialtyRules result;
	result.learningPercentPerLevel = static_cast<int>(row["learningPercentPerLevel"].Integer());
	result.learningMaximumPercent = static_cast<int>(row["learningMaximumPercent"].Integer());
	return result;
}
std::optional<LearningExchangeSpecialtyRules> learningExchangeSpecialtyRules(const CGHeroInstance * hero)
{
	if(!hero)
		return std::nullopt;
	const auto & development = hero->getPrimaryGrowthRules();
	validateLearningExchangeSpecialtiesSerialization(development, true);
	if(hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:sanya")))
		return learningExchangeSpecialtyRules(development, LearningExchangeSpecialtyKind::ECUMENICAL_SCHOLAR);
	if(hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:geon")))
		return learningExchangeSpecialtyRules(development, LearningExchangeSpecialtyKind::OCCULT_SCHOLAR);
	return std::nullopt;
}
}
