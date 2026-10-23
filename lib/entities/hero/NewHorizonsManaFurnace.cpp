/*
 * NewHorizonsManaFurnace.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
namespace
{
int32_t levelLimit(const CGHeroInstance & hero, const ManaFurnaceRules & rules)
{
	return static_cast<int32_t>(std::min<int64_t>(rules.maximumRestorePoints,
		static_cast<int64_t>(hero.level) * rules.restorePointsPerLevel));
}
}
std::optional<ManaFurnaceRules> manaFurnaceRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("additionalSpecialties"))
		return std::nullopt;
	validateAdditionalSpecialtiesSerialization(development, true);
	const auto & table = development["additionalSpecialties"];
	if(!table.Struct().contains("manaFurnace"))
		return std::nullopt;
	const auto & row = table["manaFurnace"];
	return ManaFurnaceRules{static_cast<int>(row["restorePointsPerLevel"].Integer()),
		static_cast<int>(row["maximumRestorePoints"].Integer())};
}

int32_t manaFurnaceRestoration(const CGHeroInstance * hero, const ManaFurnaceRules & rules)
{
	if(rules.restorePointsPerLevel < 0 || rules.restorePointsPerLevel > 1000
		|| rules.maximumRestorePoints < 0 || rules.maximumRestorePoints > 1000000)
		throw std::invalid_argument("Invalid Mana Furnace coefficients");
	if(!hero || !hero->getHeroTypeID().hasValue()
		|| hero->getHeroTypeID() != HeroTypeID(HeroTypeID::decode("core:ayden"))
		|| !hero->getPerkState().hasSelection("new-horizons:wisdom", "new-horizons:wisdom.intelligence")
		|| !hero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.intelligence"))
		return 0;
	const int64_t missing = std::max<int64_t>(0, static_cast<int64_t>(hero->manaLimit()) - hero->getNormalSpellPoints());
	return static_cast<int32_t>(std::min<int64_t>(missing, levelLimit(*hero, rules)));
}

bool resolveManaFurnacePresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(hero.getJsonKey() != "core:ayden")
		return false;
	const auto rules = manaFurnaceRules(development);
	if(!rules)
		return false;
	result.name = "Mana Furnace";
	result.artwork = "newHorizons/heroSpecialties/AydenManaFurnace";
	result.description = "With Intelligence active, restore " + std::to_string(rules->restorePointsPerLevel)
		+ " Normal Spell Points per hero level when combat begins, up to " + std::to_string(rules->maximumRestorePoints)
		+ " and never above the current maximum. Buffer and maximum capacity are unchanged. "
		"Unspent recovered points remain after combat.";
	if(capturedHero)
		result.description += " Current level limit: "
			+ std::to_string(levelLimit(*capturedHero, *rules)) + " Normal Spell Points.";
	return true;
}
}
