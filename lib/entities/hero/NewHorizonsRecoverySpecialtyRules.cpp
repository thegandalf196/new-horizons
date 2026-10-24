/*
 * NewHorizonsRecoverySpecialtyRules.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRecoverySpecialties.h"
#include "NewHorizonsHerbalSurgeon.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include <utility>
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
namespace
{
const char * rowName(RecoverySpecialtyKind kind)
{
	switch(kind)
	{
	case RecoverySpecialtyKind::INFERNAL_WELLSPRING: return "infernalWellspring";
	case RecoverySpecialtyKind::RITUAL_RESERVE: return "ritualReserve";
	case RecoverySpecialtyKind::DEEP_TRANCE: return "deepTrance";
	}
	throw std::invalid_argument("Unknown recovery specialty kind");
}
const char * extraName(RecoverySpecialtyKind kind)
{
	switch(kind)
	{
	case RecoverySpecialtyKind::INFERNAL_WELLSPRING: return "victoryPercent";
	case RecoverySpecialtyKind::RITUAL_RESERVE: return "lowNormalPercent";
	case RecoverySpecialtyKind::DEEP_TRANCE: return "firstCombatPercent";
	}
	throw std::invalid_argument("Unknown recovery specialty kind");
}
}
void validateRecoverySpecialtiesSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("recoverySpecialties"))
		return;
	if(!supported)
		throw std::runtime_error("Recovery specialties require the new save format");
	const auto & table = development["recoverySpecialties"];
	detail::fields(table, {"version", "infernalWellspring", "ritualReserve", "deepTrance", "herbalSurgeon"});
	detail::require(detail::integer(table["version"], 1, 1), "recovery specialty version");
	validateHerbalSurgeonRow(table);
	for(const auto kind : {RecoverySpecialtyKind::INFERNAL_WELLSPRING, RecoverySpecialtyKind::RITUAL_RESERVE, RecoverySpecialtyKind::DEEP_TRANCE})
	{
		if(!table.Struct().contains(rowName(kind)))
			continue;
		const auto & row = table[rowName(kind)];
		detail::fields(row, {"dailyPercentPerLevel", "dailyMaximumPercent", extraName(kind)});
		detail::require(detail::integer(row["dailyPercentPerLevel"], 0, 1000), "recovery daily percentage per level");
		detail::require(detail::integer(row["dailyMaximumPercent"], 0, 200), "recovery daily maximum percentage");
		detail::require(detail::integer(row[extraName(kind)], 0, 1000), "recovery specialty extra percentage");
	}
}
std::optional<RecoverySpecialtyRules> recoverySpecialtyRules(const JsonNode & development, RecoverySpecialtyKind kind)
{
	if(!usesRules(development) || !development.isStruct() || !development.Struct().contains("recoverySpecialties"))
		return std::nullopt;
	validateRecoverySpecialtiesSerialization(development, true);
	const auto & table = development["recoverySpecialties"];
	if(!table.Struct().contains(rowName(kind)))
		return std::nullopt;
	const auto & row = table[rowName(kind)];
	RecoverySpecialtyRules result;
	result.dailyPercentPerLevel = static_cast<int>(row["dailyPercentPerLevel"].Integer());
	result.dailyMaximumPercent = static_cast<int>(row["dailyMaximumPercent"].Integer());
	const auto extra = static_cast<int>(row[extraName(kind)].Integer());
	switch(kind)
	{
	case RecoverySpecialtyKind::INFERNAL_WELLSPRING: result.victoryPercent = extra; break;
	case RecoverySpecialtyKind::RITUAL_RESERVE: result.lowNormalPercent = extra; break;
	case RecoverySpecialtyKind::DEEP_TRANCE: result.firstCombatPercent = extra; break;
	}
	return result;
}
std::optional<RecoverySpecialtyRules> recoverySpecialtyRules(const CGHeroInstance & hero)
{
	if(!hero.getHeroTypeID().hasValue())
		return std::nullopt;
	for(const auto & [name, kind] : {std::pair{"core:axsis", RecoverySpecialtyKind::INFERNAL_WELLSPRING},
		std::pair{"core:jaegar", RecoverySpecialtyKind::RITUAL_RESERVE}, std::pair{"core:rosic", RecoverySpecialtyKind::DEEP_TRANCE}})
		if(hero.getHeroTypeID() == HeroTypeID(HeroTypeID::decode(name)))
			return recoverySpecialtyRules(hero.getPrimaryGrowthRules(), kind);
	return std::nullopt;
}
bool hasCapturedDeepTrance(const CGHeroInstance & hero)
{
	validateRecoverySpecialtiesSerialization(hero.getPrimaryGrowthRules(), true);
	return hero.getHeroTypeID().hasValue()
		&& hero.getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:rosic"))
		&& recoverySpecialtyRules(hero.getPrimaryGrowthRules(), RecoverySpecialtyKind::DEEP_TRANCE).has_value();
}
}
