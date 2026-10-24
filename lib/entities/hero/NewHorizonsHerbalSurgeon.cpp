/*
 * NewHorizonsHerbalSurgeon.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHerbalSurgeon.h"
#include "NewHorizonsRecoverySpecialties.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
namespace
{
bool isVerdish(const CGHeroInstance * hero)
{
	return hero && hero->getHeroTypeID().hasValue()
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:verdish"));
}
int scaledPercent(uint32_t level, int perLevel, int maximum)
{
	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(level) * perLevel, maximum));
}
}
void validateHerbalSurgeonRow(const JsonNode & table)
{
	detail::require(table.isStruct(), "recovery specialty table");
	if(!table.Struct().contains("herbalSurgeon"))
		return;
	const auto & row = table["herbalSurgeon"];
	detail::fields(row, {"tentPercentPerLevel", "tentMaximumPercent", "regenerationPercentPerLevel", "regenerationMaximumPercent"});
	for(const auto key : {"tentPercentPerLevel", "regenerationPercentPerLevel"})
		detail::require(detail::integer(row[key], 0, 1000), "Herbal Surgeon percentage per level");
	for(const auto key : {"tentMaximumPercent", "regenerationMaximumPercent"})
		detail::require(detail::integer(row[key], 0, 200), "Herbal Surgeon maximum percentage");
}
std::optional<HerbalSurgeonRules> herbalSurgeonRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct() || !development.Struct().contains("recoverySpecialties"))
		return std::nullopt;
	validateRecoverySpecialtiesSerialization(development, true);
	const auto & table = development["recoverySpecialties"];
	if(!table.Struct().contains("herbalSurgeon"))
		return std::nullopt;
	const auto & row = table["herbalSurgeon"];
	return HerbalSurgeonRules{static_cast<int>(row["tentPercentPerLevel"].Integer()), static_cast<int>(row["tentMaximumPercent"].Integer()),
		static_cast<int>(row["regenerationPercentPerLevel"].Integer()), static_cast<int>(row["regenerationMaximumPercent"].Integer())};
}
int herbalSurgeonRegenerationPercent(const CGHeroInstance * hero)
{
	if(!isVerdish(hero))
		return 0;
	const auto rule = herbalSurgeonRules(hero->getPrimaryGrowthRules());
	return rule ? scaledPercent(hero->level, rule->regenerationPercentPerLevel, rule->regenerationMaximumPercent) : 0;
}
int64_t tentCreatureHealingBudget(int64_t ordinary, const CGHeroInstance * hero)
{
	if(ordinary < 0)
		throw std::invalid_argument("Negative Tent creature-healing budget");
	if(!isVerdish(hero))
		return fieldPhysicianHealing(ordinary, hero);
	const auto rule = herbalSurgeonRules(hero->getPrimaryGrowthRules());
	if(!rule)
		return ordinary;
	const auto extra = druidSpecialtyExtra(ordinary, scaledPercent(hero->level, rule->tentPercentPerLevel, rule->tentMaximumPercent));
	return ordinary > std::numeric_limits<int64_t>::max() - extra
		? std::numeric_limits<int64_t>::max() : ordinary + extra;
}
}
