/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsAcademySpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsLevelScaledRows.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../nativeRules/CompiledDevelopmentRules.h"
#include "../../bonuses/Bonus.h"
#include "../../CCreatureHandler.h"
#include "../../mapObjects/army/CStackInstance.h"
#include "../../bonuses/BonusSelector.h"
#include "../../spells/CSpell.h"

namespace newHorizonsHeroes
{
namespace
{
constexpr auto ACADEMY_ROW = "revisedSpecialties";
constexpr auto NEELA = "core:neela";
constexpr auto RISSA = "core:rissa";
constexpr auto ASTRAL = "core:astral";
constexpr auto AINE = "core:aine";
constexpr auto SOLMYR = "core:solmyr";
constexpr auto FAFNER = "core:fafner";
constexpr auto JOSEPHINE = "core:josephine";
constexpr auto HALON = "core:halon";
constexpr auto CYRA = "core:cyra";
constexpr auto THANE = "core:thane";
constexpr auto THEODORUS = "core:theodorus";

/// getJsonKey returns by value; compare the held string, never a view of it.
bool isAcademyHero(const CGHeroInstance * hero, const char * key)
{
	return hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == key;
}

int academyHeroLevel(const CGHeroInstance * hero)
{
	return hero ? std::max(1, static_cast<int>(hero->level)) : 1;
}

/// The captured rules of `key`'s own row, or nothing for any other hero.
const AcademySpecialtyRules * academyRules(const CGHeroInstance * hero, const char * key)
{
	return isAcademyHero(hero, key) && hero->compiledDevelopment()
		? &hero->compiledDevelopment()->academySpecialties : nullptr;
}

int academyInteger(const JsonNode & row, const char * key)
{
	return static_cast<int>(row[key].Integer());
}

const JsonNode * academyChild(const JsonNode & row, const char * key)
{
	return row.Struct().contains(key) ? &row[key] : nullptr;
}

void validateAcademyCombatRows(const JsonNode & row)
{
	if(const auto * hardened = academyChild(row, "hardened"))
	{
		detail::fields(*hardened, {"hitPermille", "totalPermille", "totalPermillePerLevel", "maximumTotalPermille"});
		detail::require(detail::integer((*hardened)["hitPermille"], 1, 750), "Hardened reduction per hit");
		detail::requireScaledRow(*hardened, "totalPermille", "totalPermillePerLevel", "maximumTotalPermille", 750,
			"Hardened total reduction");
	}
	if(const auto * practice = academyChild(row, "practiceMakesPerfect"))
	{
		detail::fields(*practice, {"percentPerCast", "capPercentPerLevel"});
		detail::require(detail::integer((*practice)["percentPerCast"], 1, 10)
			&& detail::integer((*practice)["capPercentPerLevel"], 1, 10), "Practice Makes Perfect");
	}
	if(const auto * storm = academyChild(row, "stormLord"))
	{
		detail::fields(*storm, {"damagePercent", "damagePercentPerLevel", "maximumDamagePercent", "chainLightningLevel"});
		detail::requireScaledRow(*storm, "damagePercent", "damagePercentPerLevel", "maximumDamagePercent", 200,
			"Storm Lord damage");
		detail::require(detail::integer((*storm)["chainLightningLevel"], 1, 100), "Storm Lord Chain Lightning level");
	}
	if(const auto * bound = academyChild(row, "serpentsEmbraceBound"))
	{
		detail::fields(*bound, {"damagePercent", "damagePercentPerLevel", "maximumDamagePercent"});
		detail::requireScaledRow(*bound, "damagePercent", "damagePercentPerLevel", "maximumDamagePercent", 200,
			"Serpent's Embrace bound damage");
	}
	if(const auto * knowledge = academyChild(row, "inexhaustibleKnowledge"))
	{
		detail::fields(*knowledge, {"manaPermille", "manaPermillePerLevel", "maximumManaPermille", "minimumMana"});
		detail::requireScaledRow(*knowledge, "manaPermille", "manaPermillePerLevel", "maximumManaPermille", 1000,
			"Inexhaustible Knowledge Mana");
		detail::require(detail::integer((*knowledge)["minimumMana"], 0, 100), "Inexhaustible Knowledge minimum Mana");
	}
	if(const auto * weaver = academyChild(row, "spellweaver")) // Retired; still loads.
	{
		detail::fields(*weaver, {"additionalUses", "increasedAdditionalUses", "increasedFromLevel"});
		detail::require(detail::integer((*weaver)["additionalUses"], 1, 3)
			&& detail::integer((*weaver)["increasedAdditionalUses"], 1, 5)
			&& (*weaver)["increasedAdditionalUses"].Integer() >= (*weaver)["additionalUses"].Integer()
			&& detail::integer((*weaver)["increasedFromLevel"], 1, 100), "Spellweaver uses");
	}
	if(const auto * shot = academyChild(row, "halonNullifyingShot"))
	{
		detail::fields(*shot, {"enabled"});
		detail::require((*shot)["enabled"].isBool() && (*shot)["enabled"].Bool(), "Halon Nullifying Shot");
	}
	if(const auto * focus = academyChild(row, "focusMagicSpecialty"))
	{
		detail::fields(*focus, {"rangedPermille", "rangedPermillePerLevel", "maximumRangedPermille"});
		detail::requireScaledRow(*focus, "rangedPermille", "rangedPermillePerLevel", "maximumRangedPermille", 1000,
			"Focus Magic specialty ranged damage");
	}
}

void validateAcademyAdventureRows(const JsonNode & row)
{
	if(const auto * spoils = academyChild(row, "spoilsOfAlchemy"))
	{
		detail::fields(*spoils, {"goldPerMercury", "goldPerMercuryPerLevel", "minimumGoldPerMercury", "maximumMercury"});
		detail::require(detail::integer((*spoils)["goldPerMercury"], 1, 100000)
			&& detail::integer((*spoils)["goldPerMercuryPerLevel"], 0, 10000)
			&& detail::integer((*spoils)["minimumGoldPerMercury"], 1, 100000)
			&& (*spoils)["minimumGoldPerMercury"].Integer() <= (*spoils)["goldPerMercury"].Integer()
			&& detail::integer((*spoils)["maximumMercury"], 1, 100), "Spoils of Alchemy");
	}
	if(const auto * wealth = academyChild(row, "wellspringOfWealth"))
	{
		detail::fields(*wealth, {"goldPerManaPermille", "goldPerManaPermillePerLevel", "maximumGoldPerManaPermille"});
		detail::require(detail::integer((*wealth)["goldPerManaPermille"], 1, 100000)
			&& detail::integer((*wealth)["goldPerManaPermillePerLevel"], 0, 10000)
			&& detail::integer((*wealth)["maximumGoldPerManaPermille"], 1, 100000)
			&& (*wealth)["maximumGoldPerManaPermille"].Integer() >= (*wealth)["goldPerManaPermille"].Integer(),
			"Wellspring of Wealth");
	}
	if(const auto * discount = academyChild(row, "goldStandardDiscount"))
	{
		detail::fields(*discount, {"discountPercent", "discountPercentPerLevel", "maximumDiscountPercent"});
		detail::requireScaledRow(*discount, "discountPercent", "discountPercentPerLevel", "maximumDiscountPercent", 90,
			"Gold Standard upgrade discount");
	}
	if(const auto * sage = academyChild(row, "sageOfTheAges"))
	{
		detail::fields(*sage, {"spellPowerPerLevel", "knowledgePerLevel"});
		detail::require(detail::integer((*sage)["spellPowerPerLevel"], 0, 10)
			&& detail::integer((*sage)["knowledgePerLevel"], 0, 10), "Sage of the Ages growth");
	}
}
}

void validateAcademySpecialtyRows(const JsonNode & row)
{
	validateAcademyCombatRows(row);
	validateAcademyAdventureRows(row);
}

AcademySpecialtyRules academySpecialtyRules(const JsonNode & development)
{
	AcademySpecialtyRules result;
	if(!development.isStruct() || !development.Struct().contains(ACADEMY_ROW) || !usesRules(development)
		|| !development[ACADEMY_ROW].isStruct())
		return result;
	const auto & row = development[ACADEMY_ROW];
	validateAcademySpecialtyRows(row);
	if(const auto * hardened = academyChild(row, "hardened"))
		result.hardened = HardenedRules{academyInteger(*hardened, "hitPermille"),
			detail::scaledRow(*hardened, "totalPermille", "totalPermillePerLevel", "maximumTotalPermille")};
	if(const auto * spoils = academyChild(row, "spoilsOfAlchemy"))
		result.spoilsOfAlchemy = SpoilsOfAlchemyRules{academyInteger(*spoils, "goldPerMercury"),
			academyInteger(*spoils, "goldPerMercuryPerLevel"), academyInteger(*spoils, "minimumGoldPerMercury"),
			academyInteger(*spoils, "maximumMercury")};
	if(const auto * practice = academyChild(row, "practiceMakesPerfect"))
		result.practiceMakesPerfect = PracticeMakesPerfectRules{academyInteger(*practice, "percentPerCast"),
			academyInteger(*practice, "capPercentPerLevel")};
	if(const auto * wealth = academyChild(row, "wellspringOfWealth"))
		result.wellspringOfWealth = WellspringOfWealthRules{academyInteger(*wealth, "goldPerManaPermille"),
			academyInteger(*wealth, "goldPerManaPermillePerLevel"), academyInteger(*wealth, "maximumGoldPerManaPermille")};
	if(const auto * storm = academyChild(row, "stormLord"))
		result.stormLord = StormLordRules{detail::scaledRow(*storm, "damagePercent", "damagePercentPerLevel",
			"maximumDamagePercent"), academyInteger(*storm, "chainLightningLevel")};
	if(const auto * bound = academyChild(row, "serpentsEmbraceBound"))
		result.serpentsEmbraceBoundPercent = detail::scaledRow(*bound, "damagePercent", "damagePercentPerLevel",
			"maximumDamagePercent");
	if(const auto * discount = academyChild(row, "goldStandardDiscount"))
		result.goldStandardDiscountPercent = detail::scaledRow(*discount, "discountPercent", "discountPercentPerLevel",
			"maximumDiscountPercent");
	if(const auto * knowledge = academyChild(row, "inexhaustibleKnowledge"))
		result.inexhaustibleKnowledge = InexhaustibleKnowledgeRules{detail::scaledRow(*knowledge, "manaPermille",
			"manaPermillePerLevel", "maximumManaPermille"), academyInteger(*knowledge, "minimumMana")};
	result.halonNullifyingShot = academyChild(row, "halonNullifyingShot") != nullptr;
	if(const auto * weaver = academyChild(row, "spellweaver"))
		result.spellweaver = SpellweaverRules{academyInteger(*weaver, "additionalUses"),
			academyInteger(*weaver, "increasedAdditionalUses"), academyInteger(*weaver, "increasedFromLevel")};
	if(const auto * focus = academyChild(row, "focusMagicSpecialty"))
		result.focusMagicPermille = detail::scaledRow(*focus, "rangedPermille", "rangedPermillePerLevel",
			"maximumRangedPermille");
	if(const auto * sage = academyChild(row, "sageOfTheAges"))
		result.sageOfTheAges = SageOfTheAgesRules{academyInteger(*sage, "spellPowerPerLevel"),
			academyInteger(*sage, "knowledgePerLevel")};
	return result;
}

int hardenedHitPermille(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, NEELA);
	return rules && rules->hardened ? rules->hardened->hitPermille : 0;
}

int hardenedTotalPermille(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, NEELA);
	return rules && rules->hardened ? rules->hardened->totalPermille.at(academyHeroLevel(hero)) : 0;
}

int spoilsGoldPerMercury(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, RISSA);
	if(!rules || !rules->spoilsOfAlchemy)
		return 0;
	const auto & spoils = *rules->spoilsOfAlchemy;
	// The table's own formula: 2000 - 50 x level, at least 1000 (reached at level 20).
	const int64_t divisor = int64_t{spoils.goldPerMercury} - int64_t{spoils.goldPerMercuryPerLevel} * academyHeroLevel(hero);
	return static_cast<int>(std::max<int64_t>(spoils.minimumGoldPerMercury, divisor));
}

int spoilsOfAlchemyMercury(const CGHeroInstance * hero, int64_t destroyedGold)
{
	const int divisor = spoilsGoldPerMercury(hero);
	if(divisor <= 0 || destroyedGold <= 0)
		return 0;
	return static_cast<int>(std::min<int64_t>(academyRules(hero, RISSA)->spoilsOfAlchemy->maximumMercury,
		destroyedGold / divisor));
}

int64_t spoilsRecruitmentGold(const CCreatureSet & army)
{
	int64_t result = 0;
	for(const auto & [slot, stack] : army.Slots())
		if(const auto * creature = stack ? stack->getCreature() : nullptr; creature && creature->warMachine == ArtifactID::NONE)
			result += int64_t{creature->getFullRecruitCost()[EGameResID::GOLD]} * stack->getCount();
	return result;
}

int practiceCapPercent(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, ASTRAL);
	return rules && rules->practiceMakesPerfect
		? rules->practiceMakesPerfect->capPercentPerLevel * academyHeroLevel(hero) : 0;
}

int practicePercentPerCast(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, ASTRAL);
	return rules && rules->practiceMakesPerfect ? rules->practiceMakesPerfect->percentPerCast : 0;
}

namespace
{
std::string practiceStacking(SpellID spell)
{
	return "new-horizons:academy-specialty:practice:" + spell.toSpell()->getJsonKey();
}
}

int practicePercent(const CGHeroInstance * hero, SpellID spell)
{
	if(practiceCapPercent(hero) <= 0 || !spell.hasValue() || !spell.toSpell())
		return 0;
	const auto stacking = practiceStacking(spell);
	int result = 0;
	for(const auto & bonus : hero->getExportedBonusList())
		if(bonus && bonus->type == BonusType::NONE && bonus->stacking == stacking)
			result = std::max(result, bonus->val);
	return std::min(result, practiceCapPercent(hero));
}

std::optional<int> practiceAfterCast(const CGHeroInstance * hero, SpellID spell)
{
	const int cap = practiceCapPercent(hero);
	if(cap <= 0 || !spell.hasValue() || !spell.toSpell())
		return std::nullopt;
	const int current = practicePercent(hero, spell);
	const int next = std::min(cap, current + practicePercentPerCast(hero));
	return next > current ? std::optional(next) : std::nullopt;
}

void setSpellPractice(CGHeroInstance & hero, SpellID spell, int percent)
{
	if(!spell.hasValue() || !spell.toSpell() || percent < 0 || percent > 1000)
		throw std::runtime_error("Invalid Practice Makes Perfect state");
	const auto stacking = practiceStacking(spell);
	hero.removeBonuses(CSelector([&stacking](const Bonus * bonus)
		{ return bonus && bonus->type == BonusType::NONE && bonus->stacking == stacking; }));
	if(percent == 0)
		return;
	auto marker = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NONE, BonusSource::HERO_SPECIAL, percent,
		BonusSourceID(hero.getHeroTypeID()));
	marker->stacking = stacking;
	hero.addNewBonus(marker);
}

int wellspringGoldPerManaPermille(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, AINE);
	if(!rules || !rules->wellspringOfWealth)
		return 0;
	const auto & wealth = *rules->wellspringOfWealth;
	// The table's own formula: x(1.5 + 0.075 x level), x3 at level 20 and above.
	const int64_t permille = wealth.goldPerManaPermille + int64_t{wealth.goldPerManaPermillePerLevel} * academyHeroLevel(hero);
	return static_cast<int>(std::min<int64_t>(wealth.maximumGoldPerManaPermille, permille));
}

int wellspringOfWealthGold(const CGHeroInstance & hero)
{
	const int permille = wellspringGoldPerManaPermille(&hero);
	if(permille <= 0)
		return 0;
	const int64_t mana = std::max(0, hero.getNormalSpellPoints());
	return static_cast<int>(std::min<int64_t>(std::numeric_limits<int32_t>::max(), mana * permille / 1000));
}

int stormLordDamagePercent(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, SOLMYR);
	return rules && rules->stormLord ? rules->stormLord->damagePercent.at(academyHeroLevel(hero)) : 0;
}

int stormLordChainLightningLevel(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, SOLMYR);
	return rules && rules->stormLord ? rules->stormLord->chainLightningLevel : 0;
}

bool isStormLordSpell(SpellID spell)
{
	return spell == SpellID::LIGHTNING_BOLT || spell == SpellID::CHAIN_LIGHTNING;
}

int serpentsEmbraceBoundPercent(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, FAFNER);
	return rules && rules->serpentsEmbraceBoundPercent ? rules->serpentsEmbraceBoundPercent->at(academyHeroLevel(hero)) : 0;
}

int goldStandardDiscountPercent(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, JOSEPHINE);
	return rules && rules->goldStandardDiscountPercent ? rules->goldStandardDiscountPercent->at(academyHeroLevel(hero)) : 0;
}

int inexhaustibleKnowledgeMana(const CGHeroInstance & hero)
{
	const auto * rules = academyRules(&hero, THEODORUS);
	if(!rules || !rules->inexhaustibleKnowledge)
		return 0;
	const int64_t maximum = std::max<int64_t>(0, hero.manaLimit());
	const int64_t missing = maximum - std::max(0, hero.getNormalSpellPoints());
	if(missing <= 0)
		return 0;
	const auto & knowledge = *rules->inexhaustibleKnowledge;
	const int64_t regained = std::max<int64_t>(knowledge.minimumMana,
		maximum * knowledge.manaPermille.at(academyHeroLevel(&hero)) / 1000);
	return static_cast<int>(std::min(regained, missing));
}

const char * nullifyingShotHero(const CGHeroInstance * hero)
{
	return hero && hero->compiledDevelopment() && hero->compiledDevelopment()->academySpecialties.halonNullifyingShot
		? HALON : THEODORUS;
}

std::optional<int> spellweaverAdditionalUses(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, HALON);
	if(!rules || !rules->spellweaver || rules->halonNullifyingShot)
		return std::nullopt;
	const auto & weaver = *rules->spellweaver;
	return academyHeroLevel(hero) >= weaver.increasedFromLevel ? weaver.increasedAdditionalUses : weaver.additionalUses;
}

bool halonHoldsNullifyingShot(const CGHeroInstance * hero)
{
	return academyRules(hero, HALON) && academyRules(hero, HALON)->halonNullifyingShot;
}

std::optional<int> focusMagicSpecialtyPermille(const CGHeroInstance * hero)
{
	const auto * rules = academyRules(hero, CYRA);
	return rules && rules->focusMagicPermille ? std::optional(rules->focusMagicPermille->at(academyHeroLevel(hero)))
		: std::nullopt;
}

std::optional<std::array<int, GameConstants::PRIMARY_SKILLS>> sageOfTheAgesGrowth(const CGHeroInstance & hero)
{
	const auto * rules = academyRules(&hero, THANE);
	if(!rules || !rules->sageOfTheAges)
		return std::nullopt;
	return std::array<int, GameConstants::PRIMARY_SKILLS>{0, 0, rules->sageOfTheAges->spellPowerPerLevel,
		rules->sageOfTheAges->knowledgePerLevel};
}

void learnStormLordSpells(CGHeroInstance & hero)
{
	const int level = stormLordChainLightningLevel(&hero);
	if(level > 0 && static_cast<int>(hero.level) >= level && !hero.spellbookContainsSpell(SpellID::CHAIN_LIGHTNING))
		hero.addSpellToSpellbook(SpellID::CHAIN_LIGHTNING);
}

bool stormLordAllowsSpell(const CGHeroInstance & hero, SpellID spell)
{
	return spell == SpellID::CHAIN_LIGHTNING && stormLordChainLightningLevel(&hero) > 0;
}

bool academySpecialtyRetiresHistorical(const CGHeroInstance & hero)
{
	const auto * development = hero.compiledDevelopment();
	if(!development || !hero.getHeroType())
		return false;
	const auto & rules = development->academySpecialties;
	const std::string key = hero.getHeroType()->getJsonKey();
	return (key == NEELA && rules.hardened) || (key == RISSA && rules.spoilsOfAlchemy)
		|| (key == THEODORUS && rules.inexhaustibleKnowledge)
		|| (key == ASTRAL && rules.practiceMakesPerfect) || (key == AINE && rules.wellspringOfWealth)
		|| (key == SOLMYR && rules.stormLord);
}
}
