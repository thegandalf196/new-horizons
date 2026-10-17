/*
 * HeroCommandCoefficient.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommand.h"
#include "HeroCommandCoefficient.h"
#include "HeroCommandCoefficientMath.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/PrimarySkillPolicy.h"
#include "../mapObjects/CGHeroInstance.h"

namespace heroCommands
{
namespace
{
constexpr char COMMAND_SKILL[] = "new-horizons:command";
constexpr char AGGRESSIVE_COMMANDER_PERK[] = "new-horizons:command.aggressiveCommander";
constexpr char DEFENSIVE_COMMANDER_PERK[] = "new-horizons:command.defensiveCommander";
constexpr int BASIC_COMMANDER_EFFICIENCY_BONUS_PERCENT = 20;
}

CoefficientInputs::CoefficientInputs(const CGHeroInstance & hero, TConstBonusListPtr projectedPrimary)
	: hero(hero), projectedPrimary(std::move(projectedPrimary)) {}

CoefficientInputs CoefficientInputs::native(const CGHeroInstance & hero)
{
	return CoefficientInputs(hero, nullptr);
}

CoefficientInputs CoefficientInputs::projected(const CGHeroInstance & policy, TConstBonusListPtr primaryBonuses)
{
	if(!primaryBonuses || std::ranges::any_of(*primaryBonuses, [](const auto & record)
		{ return !record || record->type != BonusType::PRIMARY_SKILL; }))
		throw std::invalid_argument("Projected coefficient requires its actual primary query result");
	return CoefficientInputs(policy, std::move(primaryBonuses));
}

int CoefficientInputs::primary(PrimarySkill skill) const
{
	if(!projectedPrimary)
		return hero.getPrimSkillLevel(skill); // Native adapter only; no owned error fallback.
	return primarySkillPolicy::projectedValue(skill, *projectedPrimary,
		hero.usesPrimaryGrowth() ? &hero.getPrimaryGrowthRules() : nullptr);
}

int attributeEfficiencyPercent(const CGHeroInstance & hero, PrimarySkill attribute,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent, bool includeCommanderPerks)
{
	int efficiency = efficiencyPercent(hero) + std::clamp(warcastingBonusPercent, 0, 100)
		+ std::clamp(divineMandateEfficiencyBonusPercent, 0, 15);
	if(includeCommanderPerks && attribute == PrimarySkill::ATTACK
		&& hero.hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER_PERK))
		efficiency += BASIC_COMMANDER_EFFICIENCY_BONUS_PERCENT;
	if(includeCommanderPerks && attribute == PrimarySkill::DEFENSE
		&& hero.hasActivePerk(COMMAND_SKILL, DEFENSIVE_COMMANDER_PERK))
		efficiency += BASIC_COMMANDER_EFFICIENCY_BONUS_PERCENT;
	return efficiency;
}

int coefficient(const JsonNode & effect, const CoefficientInputs & inputs, int warcastingBonusPercent,
	int divineMandateEfficiencyBonusPercent, bool includeCommanderPerks)
{
	const int attackEfficiency = attributeEfficiencyPercent(inputs.policy(), PrimarySkill::ATTACK,
		warcastingBonusPercent, divineMandateEfficiencyBonusPercent, includeCommanderPerks);
	const int defenseEfficiency = attributeEfficiencyPercent(inputs.policy(), PrimarySkill::DEFENSE,
		warcastingBonusPercent, divineMandateEfficiencyBonusPercent, includeCommanderPerks);
	return coefficientMath::boundedCoefficient({effect["base"].Float(), effect["attack"].Float() * attackEfficiency / 100.0,
		effect["defense"].Float() * defenseEfficiency / 100.0},
		{1, inputs.primary(PrimarySkill::ATTACK), inputs.primary(PrimarySkill::DEFENSE)});
}

int coefficient(const JsonNode & effect, const CGHeroInstance & hero)
{
	return coefficient(effect, hero, 0);
}

int coefficient(const JsonNode & effect, const CGHeroInstance & hero, int warcastingBonusPercent)
{
	return coefficient(effect, hero, warcastingBonusPercent, 0);
}

int coefficient(const JsonNode & effect, const CGHeroInstance & hero, int warcastingBonusPercent,
	int divineMandateEfficiencyBonusPercent)
{
	return coefficient(effect, CoefficientInputs::native(hero), warcastingBonusPercent, divineMandateEfficiencyBonusPercent, true);
}
}
