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
}

void validateRevisedSpecialtiesSerialization(const JsonNode & development)
{
	if(!development.isStruct() || !development.Struct().contains(ROW))
		return;
	const auto & row = development[ROW];
	detail::fields(row, {"version", "lordOfTheCrypts"});
	detail::require(detail::integer(row["version"], 1, 1), "revised specialty version");
	if(row.Struct().contains("lordOfTheCrypts"))
	{
		const auto & crypts = row["lordOfTheCrypts"];
		detail::fields(crypts, {"skeletonGrowthPerLevel"});
		detail::require(detail::integer(crypts["skeletonGrowthPerLevel"], 1, 10), "Lord of the Crypts growth per level");
	}
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
