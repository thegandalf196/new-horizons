/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsReclaimer.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
void validateReclamationSpecialtySerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("reclamationSpecialty"))
		return;
	if(!supported)
		throw std::runtime_error("Reclamation specialty requires the new save format");
	const auto & row = development["reclamationSpecialty"];
	detail::fields(row, {"version", "conversionPercentPerLevel", "conversionMaximumPercent"});
	detail::require(detail::integer(row["version"], 1, 1), "reclamation specialty version");
	detail::require(detail::integer(row["conversionPercentPerLevel"], 0, 1000), "reclamation percentage per level");
	detail::require(detail::integer(row["conversionMaximumPercent"], 0, 100), "reclamation maximum percentage");
}
std::optional<ReclamationSpecialtyRules> reclamationSpecialtyRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("reclamationSpecialty"))
		return std::nullopt;
	validateReclamationSpecialtySerialization(development, true);
	const auto & row = development["reclamationSpecialty"];
	return ReclamationSpecialtyRules{static_cast<int>(row["conversionPercentPerLevel"].Integer()),
		static_cast<int>(row["conversionMaximumPercent"].Integer())};
}
int reclaimerConversionPercent(const CGHeroInstance & hero, const PlayerState * ownerState)
{
	if(!hero.getHeroTypeID().hasValue()
		|| hero.getHeroTypeID() != HeroTypeID(HeroTypeID::decode("core:vidomina"))
		|| !hero.usesNewHorizonsNecromancy())
		return 0;
	const auto rules = reclamationSpecialtyRules(hero.getPrimaryGrowthRules());
	if(!rules || !ownsZombieDwelling(ownerState, hero))
		return 0;
	return cappedSpecialtyContribution(hero.level, rules->conversionPercentPerLevel, rules->conversionMaximumPercent);
}
}
