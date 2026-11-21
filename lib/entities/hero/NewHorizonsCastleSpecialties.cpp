/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsCastleSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../nativeRules/CompiledDevelopmentRules.h"
#include "../../bonuses/Bonus.h"
#include "../../bonuses/BonusSelector.h"

#include <array>
#include <limits>

namespace newHorizonsHeroes
{
namespace
{
constexpr auto ROW = "revisedSpecialties";
constexpr auto ORRIN = "core:orrin";
constexpr auto EDRIC = "core:edric";
constexpr auto ADELA = "core:adela";
constexpr auto CUTHBERT = "core:cuthbert";
constexpr auto ADELAIDE = "core:adelaide";
constexpr auto SANYA = "core:sanya";
constexpr auto LOYNIS = "core:loynis";

bool isHero(const CGHeroInstance * hero, const char * key)
{
	return hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == key;
}

int heroLevel(const CGHeroInstance * hero)
{
	return hero ? std::max(1, static_cast<int>(hero->level)) : 1;
}

int integer(const JsonNode & row, const char * key)
{
	return static_cast<int>(row[key].Integer());
}

void requirePercent(const JsonNode & row, const char * key, int maximum, const std::string & detail)
{
	detail::require(detail::integer(row[key], 0, maximum), detail);
}

/// Percent rows: base and per-level in whole percent; base may be omitted.
LevelPercent percentRow(const JsonNode & row, const char * base, const char * perLevel, const char * maximum)
{
	LevelPercent result;
	result.basePermille = base ? integer(row, base) * 10 : 0;
	result.permillePerLevel = integer(row, perLevel) * 10;
	result.maximumPermille = integer(row, maximum) * 10;
	return result;
}

const JsonNode * castleRow(const JsonNode & development, const char * child)
{
	if(!development.isStruct() || !development.Struct().contains(ROW) || !usesRules(development))
		return nullptr;
	const auto & row = development[ROW];
	return row.isStruct() && row.Struct().contains(child) ? &row[child] : nullptr;
}

const nativeRules::CompiledDevelopmentRules * compiled(const CGHeroInstance * hero)
{
	return hero ? hero->compiledDevelopment() : nullptr;
}

template<typename T>
const std::optional<T> & ruleFor(const CGHeroInstance * hero, const char * key,
	std::optional<T> CastleSpecialtyRules::*member)
{
	static const std::optional<T> none;
	if(!isHero(hero, key))
		return none;
	const auto * rules = compiled(hero);
	return rules ? rules->castleSpecialties.*member : none;
}

std::string percentText(int permille)
{
	return std::to_string(permille / 10) + (permille % 10 ? "." + std::to_string(permille % 10) : "") + "%";
}

std::string scalingText(const LevelPercent & value, const char * what)
{
	return std::string(what) + " " + percentText(value.basePermille + value.permillePerLevel) + " at hero level 1, +"
		+ percentText(value.permillePerLevel) + " per level, up to " + percentText(value.maximumPermille) + ".";
}

std::string currentText(const CGHeroInstance * capturedHero, int permille)
{
	return capturedHero ? " Currently " + percentText(permille) + "." : std::string();
}
}

int LevelPercent::permilleAt(int level) const
{
	const int64_t value = int64_t{basePermille} + int64_t{permillePerLevel} * std::max(0, level);
	return static_cast<int>(std::clamp<int64_t>(value, 0, maximumPermille));
}

void validateCastleSpecialtyRows(const JsonNode & row)
{
	if(row.Struct().contains("volleyCommand"))
	{
		const auto & volley = row["volleyCommand"];
		detail::fields(volley, {"damagePercent", "damagePercentPerLevel", "maximumDamagePercent"});
		requirePercent(volley, "damagePercent", 100, "Volley Command damage");
		requirePercent(volley, "damagePercentPerLevel", 10, "Volley Command damage per level");
		requirePercent(volley, "maximumDamagePercent", 100, "Volley Command maximum damage");
	}
	if(row.Struct().contains("aerialSupremacy"))
	{
		const auto & aerial = row["aerialSupremacy"];
		detail::fields(aerial, {"counterDamagePercentPerLevel", "maximumCounterDamagePercent"});
		requirePercent(aerial, "counterDamagePercentPerLevel", 10, "Aerial Supremacy counter damage per level");
		requirePercent(aerial, "maximumCounterDamagePercent", 200, "Aerial Supremacy maximum counter damage");
	}
	for(const auto * key : {"benedictionOfArms", "sermon"})
		if(row.Struct().contains(key))
		{
			const auto & potency = row[key];
			detail::fields(potency, {"potencyPercentPerLevel", "maximumPotencyPercent"});
			requirePercent(potency, "potencyPercentPerLevel", 10, std::string(key) + " potency per level");
			requirePercent(potency, "maximumPotencyPercent", 200, std::string(key) + " maximum potency");
		}
	if(row.Struct().contains("pariah"))
	{
		const auto & pariah = row["pariah"];
		detail::fields(pariah, {"morale", "shadowPowerPercentPerLevel", "maximumShadowPowerPercent"});
		detail::require(detail::integer(pariah["morale"], -3, 0), "Pariah Morale");
		requirePercent(pariah, "shadowPowerPercentPerLevel", 10, "Pariah Shadow Spell Power per level");
		requirePercent(pariah, "maximumShadowPowerPercent", 100, "Pariah maximum Shadow Spell Power");
	}
	if(row.Struct().contains("frostMantle"))
	{
		const auto & mantle = row["frostMantle"];
		detail::fields(mantle, {"rounds", "returnedPercent", "returnedPercentPerLevel", "maximumReturnedPercent",
			"initiativePenaltyPercent", "chillRounds"});
		detail::require(detail::integer(mantle["rounds"], 1, 5), "Frost Mantle rounds");
		detail::require(detail::integer(mantle["chillRounds"], 1, 5), "Frost Mantle chill rounds");
		requirePercent(mantle, "returnedPercent", 100, "Frost Mantle returned damage");
		requirePercent(mantle, "returnedPercentPerLevel", 10, "Frost Mantle returned damage per level");
		requirePercent(mantle, "maximumReturnedPercent", 100, "Frost Mantle maximum returned damage");
		requirePercent(mantle, "initiativePenaltyPercent", 90, "Frost Mantle Initiative penalty");
	}
	if(row.Struct().contains("quickStudy"))
	{
		const auto & study = row["quickStudy"];
		detail::fields(study, {"spellPowerPercent", "spellPowerPermillePerLevel", "maximumSpellPowerPercent", "secondUseLevel"});
		requirePercent(study, "spellPowerPercent", 100, "Quick Study Spell Power");
		detail::require(detail::integer(study["spellPowerPermillePerLevel"], 0, 100), "Quick Study Spell Power per level");
		requirePercent(study, "maximumSpellPowerPercent", 100, "Quick Study maximum Spell Power");
		detail::require(detail::integer(study["secondUseLevel"], 1, 100), "Quick Study second use level");
	}
}

CastleSpecialtyRules castleSpecialtyRules(const JsonNode & development)
{
	CastleSpecialtyRules result;
	if(!development.isStruct() || !development.Struct().contains(ROW) || !usesRules(development)
		|| !development[ROW].isStruct())
		return result;
	validateCastleSpecialtyRows(development[ROW]);
	if(const auto * row = castleRow(development, "volleyCommand"))
		result.volleyCommand = percentRow(*row, "damagePercent", "damagePercentPerLevel", "maximumDamagePercent");
	if(const auto * row = castleRow(development, "aerialSupremacy"))
		result.aerialSupremacy = percentRow(*row, nullptr, "counterDamagePercentPerLevel", "maximumCounterDamagePercent");
	if(const auto * row = castleRow(development, "benedictionOfArms"))
		result.benedictionOfArms = percentRow(*row, nullptr, "potencyPercentPerLevel", "maximumPotencyPercent");
	if(const auto * row = castleRow(development, "sermon"))
		result.sermon = percentRow(*row, nullptr, "potencyPercentPerLevel", "maximumPotencyPercent");
	if(const auto * row = castleRow(development, "pariah"))
		result.pariah = PariahRules{integer(*row, "morale"),
			percentRow(*row, nullptr, "shadowPowerPercentPerLevel", "maximumShadowPowerPercent")};
	if(const auto * row = castleRow(development, "frostMantle"))
		result.frostMantle = FrostMantleRules{
			percentRow(*row, "returnedPercent", "returnedPercentPerLevel", "maximumReturnedPercent"),
			integer(*row, "rounds"), integer(*row, "initiativePenaltyPercent"), integer(*row, "chillRounds")};
	if(const auto * row = castleRow(development, "quickStudy"))
	{
		QuickStudyRules study;
		study.spellPower.basePermille = integer(*row, "spellPowerPercent") * 10;
		study.spellPower.permillePerLevel = integer(*row, "spellPowerPermillePerLevel");
		study.spellPower.maximumPermille = integer(*row, "maximumSpellPowerPercent") * 10;
		study.secondUseLevel = integer(*row, "secondUseLevel");
		result.quickStudy = study;
	}
	return result;
}

const std::optional<LevelPercent> & volleyCommandRule(const CGHeroInstance * hero)
{
	return ruleFor(hero, ORRIN, &CastleSpecialtyRules::volleyCommand);
}
const std::optional<LevelPercent> & revisedAerialSupremacyRule(const CGHeroInstance * hero)
{
	return ruleFor(hero, EDRIC, &CastleSpecialtyRules::aerialSupremacy);
}
const std::optional<LevelPercent> & benedictionOfArmsRule(const CGHeroInstance * hero)
{
	return ruleFor(hero, ADELA, &CastleSpecialtyRules::benedictionOfArms);
}
const std::optional<PariahRules> & pariahRule(const CGHeroInstance * hero)
{
	return ruleFor(hero, CUTHBERT, &CastleSpecialtyRules::pariah);
}
const std::optional<FrostMantleRules> & frostMantleRule(const CGHeroInstance * hero)
{
	return ruleFor(hero, ADELAIDE, &CastleSpecialtyRules::frostMantle);
}
const std::optional<QuickStudyRules> & quickStudySpecialtyRule(const CGHeroInstance * hero)
{
	return ruleFor(hero, SANYA, &CastleSpecialtyRules::quickStudy);
}
const std::optional<LevelPercent> & sermonRule(const CGHeroInstance * hero)
{
	return ruleFor(hero, LOYNIS, &CastleSpecialtyRules::sermon);
}

int volleyCommandDamagePercent(const CGHeroInstance * hero)
{
	const auto & rule = volleyCommandRule(hero);
	return rule ? rule->percentAt(heroLevel(hero)) : 0;
}

int aerialCounterDamagePercent(const CGHeroInstance * hero)
{
	const auto & rule = revisedAerialSupremacyRule(hero);
	return rule ? rule->percentAt(heroLevel(hero)) : 0;
}

int specialtyPrayerPotencyPercent(const CGHeroInstance * hero)
{
	if(const auto & rule = benedictionOfArmsRule(hero))
		return rule->percentAt(heroLevel(hero));
	if(const auto & rule = sermonRule(hero))
		return rule->percentAt(heroLevel(hero));
	return 0;
}

int boostedPotencyFaith(int potencyFaith, int bonusPercent)
{
	const int64_t boosted = int64_t{std::max(0, potencyFaith)} * (100 + std::max(0, bonusPercent)) / 100;
	return static_cast<int>(std::min<int64_t>(boosted, std::numeric_limits<int>::max() - 200));
}

int pariahShadowPowerPercent(const CGHeroInstance * hero)
{
	const auto & rule = pariahRule(hero);
	return rule ? rule->shadowPowerBonus.percentAt(heroLevel(hero)) : 0;
}

int frostMantleReturnedPercent(const CGHeroInstance * hero)
{
	const auto & rule = frostMantleRule(hero);
	return rule ? rule->returned.percentAt(heroLevel(hero)) : 0;
}

int quickStudySpellPowerPermille(const CGHeroInstance * hero)
{
	const auto & rule = quickStudySpecialtyRule(hero);
	return rule ? rule->spellPower.permilleAt(heroLevel(hero)) : 0;
}

int quickStudyUses(const CGHeroInstance * hero)
{
	const auto & rule = quickStudySpecialtyRule(hero);
	if(!rule)
		return 0;
	return heroLevel(hero) >= rule->secondUseLevel ? 2 : 1;
}

void installCastleSpecialtyBonuses(CGHeroInstance & hero)
{
	const auto & pariah = pariahRule(&hero);
	if(!pariah)
		return;
	static const std::string marker = "new-horizons:castle-specialty:pariah-morale";
	hero.removeBonuses(CSelector([](const Bonus * existing) { return existing && existing->stacking == marker; }));
	auto morale = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE, BonusSource::HERO_SPECIAL,
		pariah->morale, BonusSourceID(hero.getHeroTypeID()));
	morale->stacking = marker;
	hero.addNewBonus(morale);
}

bool castleSpecialtyRetiresHistorical(const CHero & hero, const JsonNode & development)
{
	static constexpr std::array<std::pair<const char *, const char *>, 6> replaced{{
		{ORRIN, "volleyCommand"}, {ADELA, "benedictionOfArms"}, {CUTHBERT, "pariah"},
		{ADELAIDE, "frostMantle"}, {SANYA, "quickStudy"}, {LOYNIS, "sermon"}}};
	for(const auto & [key, child] : replaced)
		if(hero.getJsonKey() == key)
			return castleRow(development, child) != nullptr;
	return false;
}

bool castleSpecialtyRetiresHistorical(const CGHeroInstance & hero)
{
	const auto * rules = compiled(&hero);
	if(!rules || !hero.getHeroType())
		return false;
	const auto & castle = rules->castleSpecialties;
	const auto & key = hero.getHeroType()->getJsonKey();
	return (key == ORRIN && castle.volleyCommand) || (key == ADELA && castle.benedictionOfArms)
		|| (key == CUTHBERT && castle.pariah) || (key == ADELAIDE && castle.frostMantle)
		|| (key == SANYA && castle.quickStudy) || (key == LOYNIS && castle.sermon);
}

namespace
{
bool commandPresentation(const std::string & key, const CastleSpecialtyRules & rules, int level,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(key == ORRIN && rules.volleyCommand)
	{
		result.name = "Volley Command";
		result.artwork = "newHorizons/heroSpecialties/OrrinArchery";
		result.summary = "When this hero issues Focus Fire!, every friendly shooter that has not acted fires one shot at the target.";
		result.description = "When this hero issues Focus Fire!, every friendly shooter that has not acted this round immediately "
			"fires one reduced-strength shot at the designated target. Each shot uses ammunition and receives Focus Fire!'s "
			"bonus; Crossfire and Relentless Barrage apply. Volley shots cannot trigger Overwatch, Counterfire or another "
			"volley, and the shooters keep their normal turn. " + scalingText(*rules.volleyCommand, "Shot damage")
			+ currentText(capturedHero, rules.volleyCommand->permilleAt(level));
		return true;
	}
	if(key == EDRIC && rules.aerialSupremacy)
	{
		result.name = "Aerial Supremacy";
		result.artwork = "newHorizons/heroSpecialties/EdricAerialSupremacy";
		result.creature = CreatureID(CreatureID::decode("core:griffin"));
		result.summary = "Griffins counter every ranged attack against them at any range, flying in to strike and back.";
		result.description = "Griffins and Royal Griffins answer each ranged attack made against them, at any range, by flying "
			"to the shooter, striking it in melee and returning. Each counter uses one retaliation, so Royal Griffins "
			"counter every shot. A counter fails only when no free hex is next to the shooter. "
			+ scalingText(*rules.aerialSupremacy, "Counter damage bonus")
			+ currentText(capturedHero, rules.aerialSupremacy->permilleAt(level));
		return true;
	}
	if(key == ADELA && rules.benedictionOfArms)
	{
		result.name = "Benediction of Arms";
		result.artwork = "newHorizons/heroSpecialties/AdelaBless";
		result.spell = SpellID(SpellID::BLESS);
		result.summary = "A stack this hero Blesses also receives the prayer of her next Order.";
		result.description = "After this hero casts Bless on a friendly stack, her next Order with a chosen Consecrated Arms "
			"prayer also grants that prayer to the Blessed stack as a second recipient. "
			+ scalingText(*rules.benedictionOfArms, "Prayer potency bonus")
			+ currentText(capturedHero, rules.benedictionOfArms->permilleAt(level));
		return true;
	}
	if(key == LOYNIS && rules.sermon)
	{
		result.name = "Sermon";
		result.artwork = "newHorizons/heroSpecialties/LoynisCrusade";
		result.summary = "Each Light spell this hero casts also grants a chosen Consecrated Arms prayer to a friendly stack.";
		result.description = "Each Light spell this hero casts also grants one chosen Consecrated Arms prayer to a friendly "
			"stack, as an Order does, with the same Faith. " + scalingText(*rules.sermon, "Prayer potency bonus")
			+ currentText(capturedHero, rules.sermon->permilleAt(level));
		return true;
	}
		return false;
}

bool magicPresentation(const std::string & key, const CastleSpecialtyRules & rules, int level,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(key == CUTHBERT && rules.pariah)
	{
		result.name = "Pariah";
		result.artwork = "newHorizons/heroSpecialties/CuthbertFrailty";
		result.summary = "Shadow spells grow stronger and every Light spell learned teaches a Shadow spell; the army has "
			+ std::to_string(rules.pariah->morale) + " Morale.";
		result.description = "This hero's whole army has " + std::to_string(rules.pariah->morale) + " Morale. His Shadow "
			"spells' Spell Power term is multiplied by 1 + 5% per hero level, up to x2; fixed spell bases do not scale. "
			"Whenever he learns a Light spell, he also learns a random unknown Shadow spell of the same level; casting "
			"it still needs the Shadow rank."
			+ (capturedHero ? " Currently x" + std::to_string(100 + rules.pariah->shadowPowerBonus.percentAt(level)) + "%." : "");
		return true;
	}
	if(key == ADELAIDE && rules.frostMantle)
	{
		const auto & mantle = *rules.frostMantle;
		result.name = "Frost Mantle";
		result.artwork = "newHorizons/heroSpecialties/AdelaideFrostRing";
		result.spell = SpellID(SpellID::FROST_RING);
		result.summary = "Frost Ring cast around a friendly stack cloaks it in returning cold.";
		result.description = "When this hero casts Frost Ring around a friendly stack, it is cloaked for "
			+ std::to_string(mantle.rounds) + " rounds: each melee hit against it returns part of the damage as cold "
			"damage, and the attacker loses " + std::to_string(mantle.initiativePenaltyPercent) + "% Initiative for "
			+ std::to_string(mantle.chillRounds) + " round. An enemy in the centre is only chilled. "
			+ scalingText(mantle.returned, "Returned damage") + currentText(capturedHero, mantle.returned.permilleAt(level));
		return true;
	}
	if(key == SANYA && rules.quickStudy)
	{
		const auto & study = *rules.quickStudy;
		result.name = "Quick Study";
		result.artwork = "newHorizons/heroSpecialties/SanyaEcumenicalScholar";
		result.summary = "After any hero casts a spell in combat, this hero may cast it once at its normal cost.";
		result.description = "After any hero, friend or foe, casts a spell in this combat, this hero may cast that spell "
			"once in this combat at its normal Mana cost, even if it is not in her spellbook or schools. Twice per "
			"combat from hero level " + std::to_string(study.secondUseLevel) + ". "
			+ scalingText(study.spellPower, "Copied spells use Spell Power at")
			+ currentText(capturedHero, study.spellPower.permilleAt(level));
		return true;
	}
	return false;
}
}

bool resolveCastleSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const auto rules = castleSpecialtyRules(development);
	const auto & key = hero.getJsonKey();
	const int level = capturedHero ? heroLevel(capturedHero) : 1;
	return commandPresentation(key, rules, level, capturedHero, result)
		|| magicPresentation(key, rules, level, capturedHero, result);
}
}
