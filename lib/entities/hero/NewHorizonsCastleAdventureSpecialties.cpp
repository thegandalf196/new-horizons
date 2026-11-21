/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsCastleAdventureSpecialties.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"
#include "NewHorizonsSpecialtyArtwork.h"
#include "CHero.h"
#include "../../bonuses/Bonus.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../nativeRules/CompiledDevelopmentRules.h"

namespace newHorizonsHeroes
{
namespace
{
constexpr auto ROW = "revisedSpecialties";
constexpr auto SYLVIA = "core:sylvia";
constexpr auto FORCED_MARCH_STACKING = "new-horizons:forced-march-specialty";

bool isHero(const CGHeroInstance & hero, const char * key)
{
	return hero.getHeroType() && hero.getHeroType()->getJsonKey() == key;
}

const CastleAdventureSpecialties * capturedRules(const CGHeroInstance & hero)
{
	const auto & rules = hero.compiledDevelopment()->castleAdventure;
	return rules ? &*rules : nullptr;
}

int heroLevel(const CGHeroInstance & hero)
{
	return static_cast<int>(hero.level);
}

LevelScaledValue scaled(const JsonNode & row, const char * base, const char * perLevel, const char * maximum)
{
	return {static_cast<int>(row[base].Integer()), static_cast<int>(row[perLevel].Integer()),
		static_cast<int>(row[maximum].Integer())};
}

void requireScaled(const JsonNode & row, const char * base, const char * perLevel, const char * maximum,
	int limit, const std::string & name)
{
	detail::require(detail::integer(row[base], 1, limit) && detail::integer(row[perLevel], 0, limit)
		&& detail::integer(row[maximum], 1, limit) && row[maximum].Integer() >= row[base].Integer(), name);
}

std::string growthText(const LevelScaledValue & value)
{
	const auto show = [](int amount) { return std::to_string(amount) + "%"; };
	return " (" + show(value.base) + " at level 1, +" + show(value.perLevel) + " per further level, up to "
		+ show(value.maximum) + ")";
}
}

int LevelScaledValue::at(int level) const
{
	const int64_t steps = std::max(1, level) - 1;
	return static_cast<int>(std::min<int64_t>(maximum, base + steps * perLevel));
}

void validateCastleAdventureSpecialties(const JsonNode & revisedRow)
{
	if(revisedRow.Struct().contains("forcedMarch"))
	{
		const auto & row = revisedRow["forcedMarch"];
		detail::fields(row, {"movementPercent", "movementPercentPerLevel", "maximumMovementPercent", "moralePenalty"});
		requireScaled(row, "movementPercent", "movementPercentPerLevel", "maximumMovementPercent", 500, "Forced March Movement");
		detail::require(detail::integer(row["moralePenalty"], 0, 3), "Forced March Morale penalty");
	}
}

std::optional<CastleAdventureSpecialties> castleAdventureSpecialties(const JsonNode & development)
{
	if(!development.isStruct() || !development.Struct().contains(ROW) || !usesRules(development))
		return std::nullopt;
	const auto & revised = development[ROW];
	if(!revised.isStruct())
		return std::nullopt;
	validateCastleAdventureSpecialties(revised);
	CastleAdventureSpecialties result;
	if(revised.Struct().contains("forcedMarch"))
	{
		const auto & row = revised["forcedMarch"];
		result.forcedMarchMovementPercent = scaled(row, "movementPercent", "movementPercentPerLevel", "maximumMovementPercent");
		result.forcedMarchMoralePenalty = static_cast<int>(row["moralePenalty"].Integer());
	}
	if(!result.forcedMarchMovementPercent)
		return std::nullopt;
	return result;
}

int forcedMarchMovementPercent(const CGHeroInstance & hero, int turn)
{
	const auto * rules = capturedRules(hero);
	if(!rules || !rules->forcedMarchMovementPercent || !hero.cb || !isHero(hero, SYLVIA))
		return 0;
	const auto day = hero.cb->getCalendar(hero.cb->getCalendar().getCurrentDay() + std::max(0, turn));
	return day.getDayOfWeek() == 1 ? rules->forcedMarchMovementPercent->at(heroLevel(hero)) : 0;
}

bool isForcedMarchMoralePenalty(const Bonus & bonus)
{
	return bonus.type == BonusType::MORALE && bonus.stacking == FORCED_MARCH_STACKING;
}

std::optional<Bonus> forcedMarchMoralePenalty(const CGHeroInstance & hero)
{
	const auto * rules = capturedRules(hero);
	if(!rules || !rules->forcedMarchMovementPercent || rules->forcedMarchMoralePenalty <= 0 || !isHero(hero, SYLVIA))
		return std::nullopt;
	const auto & local = hero.getExportedBonusList();
	if(std::ranges::any_of(local, [](const auto & bonus) { return bonus && isForcedMarchMoralePenalty(*bonus); }))
		return std::nullopt;
	Bonus penalty(BonusDuration::ONE_BATTLE, BonusType::MORALE, BonusSource::HERO_SPECIAL,
		-rules->forcedMarchMoralePenalty, BonusSourceID(hero.getHeroTypeID()));
	penalty.stacking = FORCED_MARCH_STACKING;
	penalty.description.appendRawString("Forced March");
	return penalty;
}

bool retiresHistoricalCastleSpecialty(const CGHeroInstance & hero)
{
	const auto * rules = capturedRules(hero);
	return rules && rules->forcedMarchMovementPercent && isHero(hero, SYLVIA);
}

bool resolveCastleAdventurePresentation(const CHero & hero, const JsonNode & development,
	const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const auto & key = hero.getJsonKey();
	if(key != SYLVIA)
		return false;
	const auto rules = castleAdventureSpecialties(development);
	if(!rules)
		return false;
	const auto current = [capturedHero](const LevelScaledValue & value)
	{ return value.at(capturedHero ? heroLevel(*capturedHero) : 1); };
	if(key == SYLVIA && rules->forcedMarchMovementPercent)
	{
		const auto & value = *rules->forcedMarchMovementPercent;
		result.name = "Forced March";
		result.skill = SecondarySkill(SecondarySkill::LOGISTICS);
		result.artwork = detail::resolvedSpecialtyArtwork(key, detail::SpecialtyArtworkFamily::Skill, "core:logistics");
		result.summary = "On the first day of each week her Movement is " + std::to_string(current(value))
			+ "% higher; her army then has -" + std::to_string(rules->forcedMarchMoralePenalty) + " Morale in its next combat.";
		result.description = "On the first day of each week, this hero's maximum land and sea Movement increases by "
			+ std::to_string(current(value)) + "%" + growthText(value) + ". Her army then has -"
			+ std::to_string(rules->forcedMarchMoralePenalty)
			+ " Morale in its next combat. The penalty lasts until that combat ends and does not stack.";
		return true;
	}
	return false;
}
}
