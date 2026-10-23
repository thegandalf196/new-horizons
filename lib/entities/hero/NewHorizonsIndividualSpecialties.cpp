/*
 * NewHorizonsIndividualSpecialties.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsSchoolSpellcraft.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"

namespace newHorizonsHeroes
{
namespace
{
using detail::fields;
using detail::integer;
using detail::require;

void validateOptionalFortuneChildSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct() || !rules.Struct().contains("fortuneChildSpecialty"))
		return;
	const auto & row = rules["fortuneChildSpecialty"];
	fields(row, {"version", "luckPerStep", "levelStep"});
	require(integer(row["version"], 1, 1), "Fortune's Child version");
	require(integer(row["luckPerStep"], 0, 1000), "Fortune's Child Luck per step");
	require(integer(row["levelStep"], 1, 1000), "Fortune's Child level step");
}

void validateOptionalArcanePreparationSpecialtyRules(const JsonNode & rules)
{
	if(!rules.isStruct() || !rules.Struct().contains("arcanePreparationSpecialty"))
		return;
	const auto & row = rules["arcanePreparationSpecialty"];
	fields(row, {"version", "initialPercent", "percentPerLevel", "maximumPercent"});
	require(integer(row["version"], 1, 1), "Arcane Preparation version");
	for(const char * key : {"initialPercent", "percentPerLevel", "maximumPercent"})
		require(integer(row[key], 0, 100), "Arcane Preparation percentage");
}


void validateOptionalRiders(const JsonNode & development)
{
	if(!development.isStruct() || !development.Struct().contains("individualSpecialties"))
		return;
	const auto & row = development["individualSpecialties"];
	fields(row, {"version", "graveReaper", "warlocksFormula"});
	require(integer(row["version"], 1, 1), "individual specialty version");
	const auto & grave = row["graveReaper"];
	fields(grave, {"percentagePointsPerLevel", "maximumPercentagePoints"});
	for(const auto key : {"percentagePointsPerLevel", "maximumPercentagePoints"})
		require(integer(grave[key], 0, 1000), "Grave Reaper percentage points");
	const auto & formula = row["warlocksFormula"];
	fields(formula, {"percentPerLevel", "maximumPercent"});
	require(integer(formula["percentPerLevel"], 0, 1000), "Warlock's Formula percentage per level");
	// Keep the composed School/cast/perk coefficient inside the existing scaler contract.
	require(integer(formula["maximumPercent"], 0, 200), "Warlock's Formula maximum percentage");
}
}

std::optional<FortuneChildSpecialtyRules> fortuneChildSpecialtyRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("fortuneChildSpecialty"))
		return std::nullopt;
	validateOptionalFortuneChildSpecialtyRules(development);
	const auto & row = development["fortuneChildSpecialty"];
	return FortuneChildSpecialtyRules{static_cast<int>(row["luckPerStep"].Integer()),
		static_cast<int>(row["levelStep"].Integer())};
}

std::optional<ArcanePreparationSpecialtyRules> arcanePreparationSpecialtyRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("arcanePreparationSpecialty"))
		return std::nullopt;
	validateOptionalArcanePreparationSpecialtyRules(development);
	const auto & row = development["arcanePreparationSpecialty"];
	return ArcanePreparationSpecialtyRules{static_cast<int>(row["initialPercent"].Integer()),
		static_cast<int>(row["percentPerLevel"].Integer()), static_cast<int>(row["maximumPercent"].Integer())};
}


void validateIndividualSpecialtiesSerialization(const JsonNode & development, bool supported, bool ridersSupported, bool schoolSupported)
{
	validateSchoolSpellcraftSerialization(development, schoolSupported);
	if(!development.isStruct())
		return;
	if(development.Struct().contains("individualSpecialties"))
	{
		if(!ridersSupported)
			throw std::runtime_error("Individual specialty riders require the new save format");
		validateOptionalRiders(development);
	}
	if(!development.Struct().contains("fortuneChildSpecialty")
		&& !development.Struct().contains("arcanePreparationSpecialty"))
		return;
	if(!supported)
		throw std::runtime_error("Individual hero specialties require the new save format");
	validateOptionalFortuneChildSpecialtyRules(development);
	validateOptionalArcanePreparationSpecialtyRules(development);
}

std::optional<IndividualSpecialtyRules> individualSpecialtyRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("individualSpecialties"))
		return std::nullopt;
	validateOptionalRiders(development);
	const auto & row = development["individualSpecialties"];
	return IndividualSpecialtyRules{
		{static_cast<int>(row["graveReaper"]["percentagePointsPerLevel"].Integer()),
			static_cast<int>(row["graveReaper"]["maximumPercentagePoints"].Integer())},
		{static_cast<int>(row["warlocksFormula"]["percentPerLevel"].Integer()),
			static_cast<int>(row["warlocksFormula"]["maximumPercent"].Integer())}};
}

int cappedSpecialtyContribution(int level, int perLevel, int maximum)
{
	if(perLevel < 0 || maximum < 0)
		throw std::invalid_argument("Invalid individual specialty scaling");
	return static_cast<int>(std::min<int64_t>(maximum, int64_t(std::max(0, level)) * perLevel));
}

int graveReaperPercentagePoints(const CGHeroInstance * hero)
{
	if(!hero || hero->getHeroType()->getJsonKey() != "core:isra"
		|| hero->getNewHorizonsNecromancyRank() <= 0)
		return 0;
	const auto policy = individualSpecialtyRules(hero->getPrimaryGrowthRules());
	return policy ? cappedSpecialtyContribution(hero->level,
		policy->graveReaper.percentagePointsPerLevel, policy->graveReaper.maximumPercentagePoints) : 0;
}

int warlocksFormulaPercent(const CGHeroInstance * hero)
{
	if(!hero || hero->getHeroType()->getJsonKey() != "core:malekith")
		return 0;
	const auto policy = individualSpecialtyRules(hero->getPrimaryGrowthRules());
	return policy ? cappedSpecialtyContribution(hero->level,
		policy->warlocksFormula.percentPerLevel, policy->warlocksFormula.maximumPercent) : 0;
}

int strengthenedSpellcraftBasisPoints(int ordinaryEfficiencyPercent, int specialtyPercent)
{
	if(ordinaryEfficiencyPercent < 100 || ordinaryEfficiencyPercent > 1000
		|| specialtyPercent < 0 || specialtyPercent > 1000)
		throw std::invalid_argument("Invalid strengthened Spellcraft contribution");
	return 10000 + (ordinaryEfficiencyPercent - 100) * (100 + specialtyPercent);
}
}
