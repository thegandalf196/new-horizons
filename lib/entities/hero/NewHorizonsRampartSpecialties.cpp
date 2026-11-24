/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRampartSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsLevelScaledRows.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../CCreatureHandler.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
#include "../../bonuses/Bonus.h"
#include "../../ResourceSet.h"
#include "../../StartInfo.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../nativeRules/CompiledDevelopmentRules.h"

#include <array>
#include <functional>

namespace newHorizonsHeroes
{
namespace
{
constexpr auto RAMPART_ROW = "revisedSpecialties";
constexpr auto MEPHALA = "core:mephala";
constexpr auto KYRRE = "core:kyrre";
constexpr auto JENOVA = "core:jenova";
constexpr auto CORONIUS = "core:coronius";
constexpr auto ULAND = "core:uland";
constexpr auto GEM = "core:gem";
constexpr auto ALAGAR = "core:alagar";
constexpr auto MELODIA = "core:melodia";
constexpr auto AERIS = "core:aeris";
constexpr auto RYLAND = "core:ryland";
constexpr auto IVOR = "core:ivor";

/// getJsonKey returns by value; compare the held string, never a view of it.
bool isRampartHero(const CGHeroInstance * hero, const char * key)
{
	return hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == key;
}

int rampartHeroLevel(const CGHeroInstance * hero)
{
	return hero ? std::max(1, static_cast<int>(hero->level)) : 1;
}

const RampartSpecialtyRules * rampartCaptured(const CGHeroInstance * hero)
{
	return hero && hero->compiledDevelopment() ? &hero->compiledDevelopment()->rampartSpecialties : nullptr;
}

int rampartInteger(const JsonNode & row, const char * key)
{
	return static_cast<int>(row[key].Integer());
}

const JsonNode * rampartChild(const JsonNode & row, const char * key)
{
	return row.Struct().contains(key) ? &row[key] : nullptr;
}

void validateCombatRows(const JsonNode & row)
{
	if(const auto * ground = rampartChild(row, "useTheGround"))
	{
		detail::fields(*ground, {"reductionPermille", "reductionPermillePerLevel", "maximumReductionPermille"});
		detail::requireScaledRow(*ground, "reductionPermille", "reductionPermillePerLevel", "maximumReductionPermille", 750,
			"Use the Ground reduction");
	}
	if(const auto * slayer = rampartChild(row, "giantSlayer"))
	{
		detail::fields(*slayer, {"damagePermillePerStep", "damagePermillePerStepPerLevel", "maximumDamagePermillePerStep"});
		detail::requireScaledRow(*slayer, "damagePermillePerStep", "damagePermillePerStepPerLevel", "maximumDamagePermillePerStep",
			1000, "Giant-Slayer damage per step");
	}
	if(const auto * remedy = rampartChild(row, "livingRemedy"))
	{
		detail::fields(*remedy, {"rounds", "healingBasisPoints", "healingBasisPointsPerLevel", "maximumHealingBasisPoints"});
		detail::require(detail::integer((*remedy)["rounds"], 1, 5), "Living Remedy rounds");
		detail::requireScaledRow(*remedy, "healingBasisPoints", "healingBasisPointsPerLevel", "maximumHealingBasisPoints", 10000,
			"Living Remedy healing");
	}
	if(const auto * brittle = rampartChild(row, "brittle"))
	{
		detail::fields(*brittle, {"damagePermille", "damagePermillePerLevel", "maximumDamagePermille"});
		detail::requireScaledRow(*brittle, "damagePermille", "damagePermillePerLevel", "maximumDamagePermille", 1000, "Brittle damage");
	}
	if(const auto * mount = rampartChild(row, "perfectMount"))
	{
		detail::fields(*mount, {"reductionPercent", "reductionPercentPerLevel", "maximumReductionPercent"});
		detail::requireScaledRow(*mount, "reductionPercent", "reductionPercentPerLevel", "maximumReductionPercent", 100,
			"Perfect Mount reduction");
	}
	if(const auto * moot = rampartChild(row, "treantmoot"))
	{
		detail::fields(*moot, {"dendroidReductionPercent", "dendroidReductionPercentPerLevel", "maximumDendroidReductionPercent"});
		detail::requireScaledRow(*moot, "dendroidReductionPercent", "dendroidReductionPercentPerLevel",
			"maximumDendroidReductionPercent", 75, "Treantmoot Dendroid reduction");
	}
	if(const auto * ambush = rampartChild(row, "forestAmbush"))
	{
		detail::fields(*ambush, {"shotDamagePercentPerLevel", "maximumShotDamagePercent"});
		detail::require(detail::integer((*ambush)["shotDamagePercentPerLevel"], 1, 10)
			&& detail::integer((*ambush)["maximumShotDamagePercent"], 1, 500), "Forest Ambush shot damage");
	}
}

void validateOtherRows(const JsonNode & row)
{
	if(const auto * elusive = rampartChild(row, "elusive"))
	{
		detail::fields(*elusive, {"eliteLevel", "championLevel"});
		detail::require(detail::integer((*elusive)["eliteLevel"], 1, 100) && detail::integer((*elusive)["championLevel"], 1, 100)
			&& (*elusive)["championLevel"].Integer() >= (*elusive)["eliteLevel"].Integer(), "Elusive guard levels");
	}
	if(const auto * patroness = rampartChild(row, "patroness"))
	{
		detail::fields(*patroness, {"incomePermille", "incomePermillePerLevel", "maximumIncomePermille"});
		detail::requireScaledRow(*patroness, "incomePermille", "incomePermillePerLevel", "maximumIncomePermille", 1000,
			"Patroness income share");
	}
	if(const auto * sorceress = rampartChild(row, "sorceressOfEnroth"))
	{
		detail::fields(*sorceress, {"baseSpellLevel", "heroLevelsPerSpellLevel"});
		detail::require(detail::integer((*sorceress)["baseSpellLevel"], 1, 5)
			&& detail::integer((*sorceress)["heroLevelsPerSpellLevel"], 1, 100), "Sorceress of Enroth spell levels");
	}
	if(const auto * odds = rampartChild(row, "againstTheOdds"))
	{
		detail::fields(*odds, {"luck", "levelsPerLuck", "maximumLuck"});
		detail::require(detail::integer((*odds)["luck"], 1, 10) && detail::integer((*odds)["levelsPerLuck"], 1, 100)
			&& detail::integer((*odds)["maximumLuck"], 1, 10) && (*odds)["maximumLuck"].Integer() >= (*odds)["luck"].Integer(),
			"Against the Odds Luck");
	}
}
}

void validateRampartSpecialtyRows(const JsonNode & row)
{
	validateCombatRows(row);
	validateOtherRows(row);
}

RampartSpecialtyRules rampartSpecialtyRules(const JsonNode & development)
{
	RampartSpecialtyRules result;
	if(!development.isStruct() || !development.Struct().contains(RAMPART_ROW) || !usesRules(development)
		|| !development[RAMPART_ROW].isStruct())
		return result;
	const auto & row = development[RAMPART_ROW];
	validateRampartSpecialtyRows(row);
	if(const auto * ground = rampartChild(row, "useTheGround"))
		result.useTheGroundPermille = detail::scaledRow(*ground, "reductionPermille", "reductionPermillePerLevel", "maximumReductionPermille");
	if(const auto * elusive = rampartChild(row, "elusive"))
		result.elusive = ElusiveRules{rampartInteger(*elusive, "eliteLevel"), rampartInteger(*elusive, "championLevel")};
	if(const auto * patroness = rampartChild(row, "patroness"))
		result.patronessPermille = detail::scaledRow(*patroness, "incomePermille", "incomePermillePerLevel", "maximumIncomePermille");
	if(const auto * slayer = rampartChild(row, "giantSlayer"))
		result.giantSlayerPermillePerStep = detail::scaledRow(*slayer, "damagePermillePerStep", "damagePermillePerStepPerLevel",
			"maximumDamagePermillePerStep");
	if(const auto * remedy = rampartChild(row, "livingRemedy"))
		result.livingRemedy = LivingRemedyRules{rampartInteger(*remedy, "rounds"),
			detail::scaledRow(*remedy, "healingBasisPoints", "healingBasisPointsPerLevel", "maximumHealingBasisPoints")};
	if(const auto * sorceress = rampartChild(row, "sorceressOfEnroth"))
		result.sorceressOfEnroth = SorceressOfEnrothRules{rampartInteger(*sorceress, "baseSpellLevel"),
			rampartInteger(*sorceress, "heroLevelsPerSpellLevel")};
	if(const auto * brittle = rampartChild(row, "brittle"))
		result.brittlePermille = detail::scaledRow(*brittle, "damagePermille", "damagePermillePerLevel", "maximumDamagePermille");
	if(const auto * odds = rampartChild(row, "againstTheOdds"))
		result.againstTheOdds = AgainstTheOddsRules{rampartInteger(*odds, "luck"), rampartInteger(*odds, "levelsPerLuck"),
			rampartInteger(*odds, "maximumLuck")};
	if(const auto * mount = rampartChild(row, "perfectMount"))
		result.perfectMountPercent = detail::scaledRow(*mount, "reductionPercent", "reductionPercentPerLevel", "maximumReductionPercent");
	if(const auto * moot = rampartChild(row, "treantmoot"))
		result.treantmootPercent = detail::scaledRow(*moot, "dendroidReductionPercent", "dendroidReductionPercentPerLevel",
			"maximumDendroidReductionPercent");
	if(const auto * ambush = rampartChild(row, "forestAmbush"))
	{
		const int perLevel = rampartInteger(*ambush, "shotDamagePercentPerLevel");
		result.forestAmbushShotPercent = LevelScaledValue{perLevel, perLevel, rampartInteger(*ambush, "maximumShotDamagePercent")};
	}
	return result;
}

namespace
{
int scaledFor(const CGHeroInstance * hero, const char * key, std::optional<LevelScaledValue> RampartSpecialtyRules::*member)
{
	const auto * rules = isRampartHero(hero, key) ? rampartCaptured(hero) : nullptr;
	if(!rules)
		return 0;
	const auto & value = std::invoke(member, *rules);
	return value ? value->at(rampartHeroLevel(hero)) : 0;
}
}

int useTheGroundPermille(const CGHeroInstance * hero)
{
	return scaledFor(hero, MEPHALA, &RampartSpecialtyRules::useTheGroundPermille);
}

std::optional<newHorizonsCreatures::CreatureCategory> elusiveGuardRank(const CGHeroInstance * hero)
{
	const auto * rules = isRampartHero(hero, KYRRE) ? rampartCaptured(hero) : nullptr;
	if(!rules || !rules->elusive)
		return std::nullopt;
	const int level = rampartHeroLevel(hero);
	using newHorizonsCreatures::CreatureCategory;
	return level >= rules->elusive->championLevel ? CreatureCategory::CHAMPION
		: level >= rules->elusive->eliteLevel ? CreatureCategory::ELITE : CreatureCategory::CORE;
}

int patronessPermille(const CGHeroInstance * hero)
{
	return scaledFor(hero, JENOVA, &RampartSpecialtyRules::patronessPermille);
}

namespace
{
std::string patronessStacking(const CGTownInstance & town)
{
	return "new-horizons:rampart-specialty:patroness:" + std::to_string(town.id.getNum());
}
}

ResourceSet patronessCollection(const CGHeroInstance & hero, const CGTownInstance & town)
{
	const int permille = patronessPermille(&hero);
	if(permille <= 0 || !hero.getOwner().isValidPlayer() || town.getOwner() != hero.getOwner())
		return {};
	const auto stacking = patronessStacking(town);
	if(std::ranges::any_of(hero.getExportedBonusList(), [&stacking](const auto & bonus)
		{ return bonus && bonus->type == BonusType::NONE && bonus->stacking == stacking; }))
		return {};
	ResourceSet result = town.dailyIncome();
	// Her own steward Gold is not the town's income to share with her.
	const int64_t ownStewardGold = town.getStewardGoldBeforeHandicap() - town.getStewardGoldBeforeHandicap(&hero);
	if(ownStewardGold > 0 && town.cb)
		if(const auto * settings = town.cb->getPlayerSettings(town.getOwner()))
			result[EGameResID::GOLD] = std::max<TResourceCap>(0,
				result[EGameResID::GOLD] - ownStewardGold * settings->handicap.percentIncome / 100);
	for(auto & amount : result)
		amount = amount > 0 ? static_cast<TResourceCap>(static_cast<int64_t>(amount) * permille / 1000) : 0;
	return result;
}

ResourceSet patronessOnNextEntry(const CGHeroInstance & hero, const CGTownInstance & town)
{
	if(town.getVisitingHero() == &hero || town.getGarrisonHero() == &hero)
		return {};
	return patronessCollection(hero, town);
}

Bonus patronessMarker(const CGHeroInstance & hero, const CGTownInstance & town)
{
	Bonus marker(BonusDuration::ONE_WEEK, BonusType::NONE, BonusSource::HERO_SPECIAL, 0,
		BonusSourceID(hero.getHeroTypeID()));
	marker.stacking = patronessStacking(town);
	return marker;
}

int giantSlayerPermillePerStep(const CGHeroInstance * hero)
{
	return scaledFor(hero, CORONIUS, &RampartSpecialtyRules::giantSlayerPermillePerStep);
}

int livingRemedyBasisPoints(const CGHeroInstance * hero)
{
	const auto * rules = isRampartHero(hero, ULAND) ? rampartCaptured(hero) : nullptr;
	return rules && rules->livingRemedy ? rules->livingRemedy->healingBasisPoints.at(rampartHeroLevel(hero)) : 0;
}

int livingRemedyRounds(const CGHeroInstance * hero)
{
	const auto * rules = isRampartHero(hero, ULAND) ? rampartCaptured(hero) : nullptr;
	return rules && rules->livingRemedy ? rules->livingRemedy->rounds : 0;
}

int sorceressFreeSpellLevel(const CGHeroInstance * hero)
{
	const auto * rules = isRampartHero(hero, GEM) ? rampartCaptured(hero) : nullptr;
	if(!rules || !rules->sorceressOfEnroth)
		return 0;
	const auto & sorceress = *rules->sorceressOfEnroth;
	return std::min(5, sorceress.baseSpellLevel + rampartHeroLevel(hero) / sorceress.heroLevelsPerSpellLevel);
}

int brittlePermille(const CGHeroInstance * hero)
{
	return scaledFor(hero, ALAGAR, &RampartSpecialtyRules::brittlePermille);
}

int againstTheOddsLuck(const CGHeroInstance * hero)
{
	const auto * rules = isRampartHero(hero, MELODIA) ? rampartCaptured(hero) : nullptr;
	if(!rules || !rules->againstTheOdds)
		return 0;
	const auto & odds = *rules->againstTheOdds;
	return std::min(odds.maximumLuck, odds.luck + rampartHeroLevel(hero) / odds.levelsPerLuck);
}

std::optional<int> perfectMountPercent(const CGHeroInstance & hero)
{
	const auto * rules = isRampartHero(&hero, AERIS) ? rampartCaptured(&hero) : nullptr;
	return rules && rules->perfectMountPercent ? std::optional(rules->perfectMountPercent->at(rampartHeroLevel(&hero))) : std::nullopt;
}

std::optional<int> treantmootPercent(const CGHeroInstance & hero)
{
	const auto * rules = isRampartHero(&hero, RYLAND) ? rampartCaptured(&hero) : nullptr;
	return rules && rules->treantmootPercent ? std::optional(rules->treantmootPercent->at(rampartHeroLevel(&hero))) : std::nullopt;
}

int forestAmbushShotPercent(const CGHeroInstance * hero)
{
	return scaledFor(hero, IVOR, &RampartSpecialtyRules::forestAmbushShotPercent);
}

int specialtyRank(const newHorizonsCreatures::CreatureCategoryRules & rules, CreatureID creature)
{
	if(const auto category = newHorizonsCreatures::creatureCategoryView(rules, creature))
		return static_cast<int>(category->category);
	const auto * type = creature.hasValue() ? creature.toCreature() : nullptr;
	const int level = type ? type->getLevel() : 0;
	return level <= 0 ? -1 : level <= 3 ? 0 : level <= 6 ? 1 : 2;
}

bool rampartSpecialtyRetiresHistorical(const CGHeroInstance & hero)
{
	const auto * rules = rampartCaptured(&hero);
	if(!rules || !hero.getHeroType())
		return false;
	const std::string key = hero.getHeroType()->getJsonKey();
	return (key == MEPHALA && rules->useTheGroundPermille) || (key == KYRRE && rules->elusive)
		|| (key == JENOVA && rules->patronessPermille) || (key == CORONIUS && rules->giantSlayerPermillePerStep)
		|| (key == ULAND && rules->livingRemedy) || (key == GEM && rules->sorceressOfEnroth)
		|| (key == ALAGAR && rules->brittlePermille) || (key == MELODIA && rules->againstTheOdds);
}

bool rampartPassiveBundleScales(const CGHeroInstance & hero)
{
	return perfectMountPercent(hero).has_value() || treantmootPercent(hero).has_value();
}
}
