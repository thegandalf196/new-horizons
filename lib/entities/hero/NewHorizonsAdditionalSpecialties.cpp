/*
 * NewHorizonsAdditionalSpecialties.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsLearningSpecialties.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
#include <limits>
namespace newHorizonsHeroes
{
void validateAdditionalSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("additionalSpecialties"))
		return;
	if(!supported)
		throw std::runtime_error("Additional individual specialties require the new save format");
	const auto & table = development["additionalSpecialties"];
	detail::fields(table, {"version", "fieldPhysician", "manaFurnace", "graveArchivist", "spiritTeacher"});
	detail::require(detail::integer(table["version"], 1, 1), "additional specialty version");
	validateLearningSpecialtyRows(table);
	if(table.Struct().contains("manaFurnace"))
	{
		const auto & row = table["manaFurnace"];
		detail::fields(row, {"restorePointsPerLevel", "maximumRestorePoints"});
		detail::require(detail::integer(row["restorePointsPerLevel"], 0, 1000), "furnace points per level");
		detail::require(detail::integer(row["maximumRestorePoints"], 0, 1000000), "furnace maximum restoration");
	}
	if(!table.Struct().contains("fieldPhysician"))
		return;
	const auto & row = table["fieldPhysician"];
	detail::fields(row, {"healingPercentPerLevel", "healingMaximumPercent"});
	detail::require(detail::integer(row["healingPercentPerLevel"], 0, 1000), "physician percentage per level");
	detail::require(detail::integer(row["healingMaximumPercent"], 0, 200), "physician maximum percentage");
}

std::optional<FieldPhysicianRules> fieldPhysicianRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("additionalSpecialties"))
		return std::nullopt;
	validateAdditionalSpecialtiesSerialization(development, true);
	const auto & table = development["additionalSpecialties"];
	if(!table.Struct().contains("fieldPhysician"))
		return std::nullopt;
	const auto & row = table["fieldPhysician"];
	return FieldPhysicianRules{static_cast<int>(row["healingPercentPerLevel"].Integer()),
		static_cast<int>(row["healingMaximumPercent"].Integer())};
}

int fieldPhysicianHealingPercent(const CGHeroInstance * hero)
{
	if(!hero || !hero->getHeroTypeID().hasValue()
		|| hero->getHeroTypeID() != HeroTypeID(HeroTypeID::decode("core:rion")))
		return 0;
	const auto rule = fieldPhysicianRules(hero->getPrimaryGrowthRules());
	return rule ? cappedSpecialtyContribution(hero->level, rule->healingPercentPerLevel, rule->healingMaximumPercent) : 0;
}

int64_t fieldPhysicianHealing(int64_t ordinary, const CGHeroInstance * hero)
{
	const auto extra = druidSpecialtyExtra(ordinary, fieldPhysicianHealingPercent(hero));
	const auto maximum = std::numeric_limits<int64_t>::max();
	return ordinary > maximum - extra ? maximum : ordinary + extra;
}

bool resolveAdditionalSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(resolveLearningSpecialtyPresentation(hero, development, magic, capturedHero, result))
		return true;
	if(hero.getJsonKey() == "core:ayden")
		return resolveManaFurnacePresentation(hero, development, capturedHero, result);
	if(hero.getJsonKey() != "core:rion")
		return false;
	const auto rule = fieldPhysicianRules(development);
	if(!rule)
		return false;
	result.name = "Field Physician";
	result.artwork = "newHorizons/heroSpecialties/RionFieldPhysician";
	const bool cureActive = newHorizonsMagic::cureEnabled(magic, SpellID::CURE);
	result.description = std::string(cureActive ? "Cure and First Aid Tent creature healing increase by "
		: "First Aid Tent creature healing increases by ")
		+ std::to_string(rule->healingPercentPerLevel) + "% per hero level, up to +"
		+ std::to_string(rule->healingMaximumPercent) + "%. ";
	result.description += cureActive ? "This includes Cure's fixed healing. Cure must be learned normally. "
		: "Cure retains its ordinary healing under this game's legacy magic rules. ";
	result.description += "This does not improve repair, remove extra afflictions or grant resurrection. "
		"Battlefield Medic uses the enhanced Tent healing budget under its ordinary rules.";
	if(capturedHero)
		result.description += std::string(cureActive ? " Current healing increase: +" : " Current Tent healing increase: +")
			+ std::to_string(fieldPhysicianHealingPercent(capturedHero)) + "%.";
	return true;
}
}
