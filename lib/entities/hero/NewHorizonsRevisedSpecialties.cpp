/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRevisedSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../bonuses/Bonus.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
#include "../../nativeRules/CompiledDevelopmentRules.h"
#include "NewHorizonsOrdinaryCreatureSpecialties.h"
#include "NewHorizonsPassiveSpecialtyBonuses.h"
#include "NewHorizonsCastleSpecialties.h"
#include "../../bonuses/BonusSelector.h"

#include <array>

namespace newHorizonsHeroes
{
namespace
{
constexpr auto ROW = "revisedSpecialties";
constexpr auto CLAVIUS = "core:clavius";

const JsonNode * revisedRow(const JsonNode & development, const char * child)
{
	if(!development.isStruct() || !development.Struct().contains(ROW) || !usesRules(development))
		return nullptr;
	const auto & row = development[ROW];
	return row.isStruct() && row.Struct().contains(child) ? &row[child] : nullptr;
}

bool isHero(const CGHeroInstance & hero, const char * key)
{
	return hero.getHeroType() && hero.getHeroType()->getJsonKey() == key;
}

enum class Growth
{
	PercentOfBase, // authored value x (1 + rate x (level - 1)), the +3%/level rule
	PerLevel, // authored value + fixed amount x (level - 1)
	PerTenLevels // authored value + fixed amount x floor(level / 10)
};

struct ScaledCoefficient
{
	std::string_view key;
	Growth growth;
	int amountPerLevel; // used by PerLevel only
	int maximum; // hard cap; physical reduction 75%, chances 100%
};

constexpr int UNCAPPED = 1000000;
// Coefficients the 2026-11-20 faction tables put on the level rule.
constexpr std::array<ScaledCoefficient, 18> scaledCoefficients{{
	{"relentlessBarrageDamagePercent", Growth::PercentOfBase, 0, UNCAPPED}, // Valeska
	{"shieldAndSwordPhysicalReductionPercent", Growth::PercentOfBase, 0, 75}, // Sorsha
	{"pointBlankShotDamageBonusPercent", Growth::PercentOfBase, 0, UNCAPPED}, // Christian
	{"lanceFormationDefensePerHex", Growth::PercentOfBase, 0, 100}, // Tyris
	{"lanceFormationMaximumDefense", Growth::PercentOfBase, 0, 100}, // Tyris
	{"consecratedTargetDefenseIgnorePercent", Growth::PercentOfBase, 0, 100}, // Ingham
	{"stoneSentinelReductionPercent", Growth::PercentOfBase, 0, 75}, // Piquedram
	{"dispellingShotMageChancePercent", Growth::PercentOfBase, 0, 100}, // Theodorus
	{"dispellingShotArchMageChancePercent", Growth::PercentOfBase, 0, 100}, // Theodorus
	{"cinderReprisalDamagePercent", Growth::PercentOfBase, 0, UNCAPPED}, // Rashka
	{"demonicSolidarityReductionPercent", Growth::PercentOfBase, 0, 75}, // Marius
	{"scorchingImpactDamageReductionPercent", Growth::PercentOfBase, 0, 75}, // Calh
	{"cruelOverseerDefenseReduction", Growth::PercentOfBase, 0, UNCAPPED}, // Nymus
	{"graveDustHealingReductionBasisPoints", Growth::PercentOfBase, 0, 10000}, // Moandor
	{"undyingHealingHP", Growth::PerLevel, 5, UNCAPPED}, // Straker: 100 HP, +5 HP per level
	{"resistanceAuraPoints", Growth::PercentOfBase, 0, 75}, // Clancy
	{"runicReflectionChancePercent", Growth::PercentOfBase, 0, 100}, // Ufretin: Dwarf 20%, Battle Dwarf 40%
	{"packPursuitSpeed", Growth::PerTenLevels, 1, UNCAPPED} // Fiona: +5 Speed, +1 per 10 levels
}};

const ScaledCoefficient * scaledCoefficient(std::string_view key)
{
	for(const auto & row : scaledCoefficients)
		if(row.key == key)
			return &row;
	return nullptr;
}
}

void validateRevisedSpecialtiesSerialization(const JsonNode & development)
{
	if(!development.isStruct() || !development.Struct().contains(ROW))
		return;
	const auto & row = development[ROW];
	detail::fields(row, {"version", "lordOfTheCrypts", "levelScaling", "volleyCommand", "aerialSupremacy",
		"benedictionOfArms", "pariah", "frostMantle", "quickStudy", "sermon"});
	detail::require(detail::integer(row["version"], 1, 1), "revised specialty version");
	if(row.Struct().contains("levelScaling"))
	{
		const auto & scaling = row["levelScaling"];
		detail::fields(scaling, {"percentOfBasePerLevel"});
		detail::require(detail::integer(scaling["percentOfBasePerLevel"], 1, 10), "specialty level scaling");
	}
	if(row.Struct().contains("lordOfTheCrypts"))
	{
		const auto & crypts = row["lordOfTheCrypts"];
		detail::fields(crypts, {"skeletonGrowthPerLevel"});
		detail::require(detail::integer(crypts["skeletonGrowthPerLevel"], 1, 10), "Lord of the Crypts growth per level");
	}
	validateCastleSpecialtyRows(row);
}

bool requiresRevisedSpecialtyFormat(const JsonNode & document)
{
	const auto claims = [](const JsonNode & rules)
	{
		return rules.isStruct() && (rules.Struct().contains(ROW) || rules.Struct().contains("spellcraftPolicyVersion")
			|| (rules["estatesPolicyVersion"].isNumber() && rules["estatesPolicyVersion"].Integer() == 2));
	};
	if(!document.isStruct())
		return false;
	if(claims(document))
		return true;
	const auto heroes = document.Struct().find("heroes");
	return heroes != document.Struct().end() && heroes->second.isStruct()
		&& ((heroes->second.Struct().contains("newHorizons") && claims(heroes->second["newHorizons"]))
			|| (heroes->second.Struct().contains("newHorizonsPerks") && claims(heroes->second["newHorizonsPerks"])));
}

std::optional<LordOfTheCryptsRules> lordOfTheCryptsRules(const JsonNode & development)
{
	const auto * row = revisedRow(development, "lordOfTheCrypts");
	if(!row)
		return std::nullopt;
	validateRevisedSpecialtiesSerialization(development);
	return LordOfTheCryptsRules{static_cast<int>((*row)["skeletonGrowthPerLevel"].Integer())};
}

std::optional<SpecialtyLevelScaling> specialtyLevelScaling(const JsonNode & development)
{
	const auto * row = revisedRow(development, "levelScaling");
	if(!row)
		return std::nullopt;
	validateRevisedSpecialtiesSerialization(development);
	return SpecialtyLevelScaling{static_cast<int>((*row)["percentOfBasePerLevel"].Integer())};
}

bool scalesWithLevel(std::string_view coefficient)
{
	return scaledCoefficient(coefficient) != nullptr;
}

int levelScaledOrdinaryValue(const SpecialtyLevelScaling & scaling, std::string_view coefficient, int authored, int level)
{
	const auto * row = scaledCoefficient(coefficient);
	if(!row || authored <= 0)
		return authored;
	const int64_t steps = std::max(1, level) - 1;
	const int64_t scaled = row->growth == Growth::PerLevel ? authored + steps * row->amountPerLevel
		: row->growth == Growth::PerTenLevels ? authored + std::max(0, level) / 10 * row->amountPerLevel
		: authored * (100 + steps * scaling.percentOfBasePerLevel) / 100;
	return static_cast<int>(std::min<int64_t>(scaled, std::max(authored, row->maximum)));
}

std::string levelScalingDescription(const SpecialtyLevelScaling & scaling, std::string_view coefficient)
{
	const auto * row = scaledCoefficient(coefficient);
	if(!row)
		return {};
	std::string text = row->growth == Growth::PerTenLevels
		? "This value grows by " + std::to_string(row->amountPerLevel) + " for every 10 hero levels"
		: row->growth == Growth::PerLevel
		? "This value applies at hero level 1 and grows by " + std::to_string(row->amountPerLevel) + " per further level"
		: "This value applies at hero level 1 and grows by " + std::to_string(scaling.percentOfBasePerLevel)
			+ "% of itself per further level, rounded down";
	if(row->maximum != UNCAPPED)
		text += coefficient.ends_with("BasisPoints") ? ", up to " + std::to_string(row->maximum) + " basis points"
			: coefficient.ends_with("Percent") ? ", up to " + std::to_string(row->maximum) + "%"
			: ", up to " + std::to_string(row->maximum);
	return text + ".";
}

int levelScaledOrdinaryValue(const CGHeroInstance & hero, std::string_view coefficient, int authored)
{
	const auto & scaling = hero.compiledDevelopment()->levelScaling;
	return scaling ? levelScaledOrdinaryValue(*scaling, coefficient, authored, static_cast<int>(hero.level)) : authored;
}

void refreshLevelScaledSpecialtyBonus(CGHeroInstance & hero)
{
	if(!hero.compiledDevelopment()->levelScaling)
		return;
	if(isHero(hero, "core:ufretin"))
	{
		// Runic Endurance is a passive bundle; replace it as a whole.
		const auto passive = passiveCreatureSpecialtyBonuses(hero);
		hero.removeBonuses(CSelector([&passive](const Bonus * existing)
		{
			return existing && std::ranges::any_of(passive, [existing](const auto & bonus)
			{ return existing->source == bonus->source && existing->sid == bonus->sid && existing->stacking == bonus->stacking; });
		}));
		for(const auto & bonus : passive)
			hero.addNewBonus(bonus);
		return;
	}
	const auto bonus = ordinaryCreatureSpecialtyBonus(hero);
	if(!bonus)
		return;
	const auto * identity = hero.compiledDevelopment()->ordinary(hero.getHeroTypeID(), true);
	if(!identity || !scalesWithLevel(identity->coefficient))
		return;
	hero.removeBonuses(CSelector([&bonus](const Bonus * existing)
	{
		return existing && existing->source == bonus->source && existing->sid == bonus->sid
			&& existing->stacking == bonus->stacking;
	}));
	hero.addNewBonus(bonus);
}

int levelScaledDreadCavalryPercent(const CGHeroInstance & hero, int authored)
{
	const auto & scaling = hero.compiledDevelopment()->levelScaling;
	if(!scaling || authored <= 0)
		return authored;
	const int64_t steps = std::max(1, static_cast<int>(hero.level)) - 1;
	return static_cast<int>(std::min<int64_t>(100, authored * (100 + steps * scaling->percentOfBasePerLevel) / 100));
}

int levelScaledManaLeeches(const CGHeroInstance & hero, int authored)
{
	const auto & scaling = hero.compiledDevelopment()->levelScaling;
	return scaling && authored > 0 ? authored + static_cast<int>(hero.level) / 10 : authored;
}

int lordOfTheCryptsGrowth(const CGHeroInstance & steward, const CGTownInstance & town, int level)
{
	if(!isHero(steward, CLAVIUS) || town.getFactionID() != FactionID::NECROPOLIS
		|| level < 0 || static_cast<size_t>(level) >= town.creatures.size())
		return 0;
	const CreatureID skeleton(CreatureID::decode("core:skeleton"));
	if(!vstd::contains(town.creatures[level].second, skeleton))
		return 0;
	const auto rules = lordOfTheCryptsRules(steward.getPrimaryGrowthRules());
	return rules ? rules->skeletonGrowthPerLevel * std::max(1, static_cast<int>(steward.level)) : 0;
}

bool retiresSpecialtyBonus(const CGHeroInstance & hero, const Bonus & bonus)
{
	// Revised Castle specialties replace these heroes' whole historical specialty.
	if(castleSpecialtyRetiresHistorical(hero))
		return true;
	// Lord of the Crypts replaces Clavius's flat daily Gold.
	return bonus.type == BonusType::GENERATE_RESOURCE && isHero(hero, CLAVIUS)
		&& lordOfTheCryptsRules(hero.getPrimaryGrowthRules()).has_value();
}

bool benefitsFromStewardship(const CGHeroInstance & hero, const CGTownInstance & town)
{
	return lordOfTheCryptsGrowth(hero, town, 0) > 0;
}

bool resolveRevisedSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(resolveCastleSpecialtyPresentation(hero, development, capturedHero, result))
		return true;
	if(hero.getJsonKey() != CLAVIUS)
		return false;
	const auto crypts = lordOfTheCryptsRules(development);
	if(!crypts)
		return false;
	const int perLevel = crypts->skeletonGrowthPerLevel;
	result.name = "Lord of the Crypts";
	result.artwork = "newHorizons/heroSpecialties/GalthranSkeletonLine";
	result.creature = CreatureID(CreatureID::decode("core:skeleton"));
	result.summary = "While steward of a Necropolis town, its weekly Skeleton growth increases by "
		+ std::to_string(perLevel) + " per hero level.";
	result.description = "While this hero is steward of a Necropolis town, the town's Skeleton dwelling grows "
		+ std::to_string(perLevel) + " additional creature" + (perLevel == 1 ? "" : "s")
		+ " per week for each hero level";
	if(capturedHero)
		result.description += " (currently +" + std::to_string(perLevel * std::max(1, static_cast<int>(capturedHero->level))) + ")";
	result.description += ". Skeletons and Skeleton Warriors share this growth and are recruited at their normal price.";
	return true;
}
}
