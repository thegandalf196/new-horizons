/*
 * HeroCommandCompanions.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommand.h"
#include "NewHorizonsCommandResources.h"
#include "HeroCommandCoefficient.h"
#include "HeroCommandCoefficientMath.h"
#include "../mapObjects/CGHeroInstance.h"

namespace heroCommands
{
namespace
{
constexpr char COMMAND_SKILL[] = "new-horizons:command";
constexpr char VETERAN_COMMANDER_PERK[] = "new-horizons:command.veteranCommander";
constexpr char COMBINED_ARMS_PERK[] = "new-horizons:command.combinedArms";
constexpr char DOUBLE_COMMAND_PERK[] = "new-horizons:command.doubleCommand";
constexpr int VETERAN_COMMANDER_EFFICIENCY_BONUS_PERCENT = 25;

}

int efficiencyPercent(const CGHeroInstance & hero)
{
	return 100 + std::clamp(hero.getPerkSkillRank("new-horizons:command"), 0, 3) * 10
		+ (newHorizonsCommand::usesPerkRules(&hero)
			&& hero.hasActivePerk(COMMAND_SKILL, DOUBLE_COMMAND_PERK) ? 15 : 0);
}

int leadershipEfficiencyPercent(const CGHeroInstance & hero,
	int divineMandateEfficiencyBonusPercent)
{
	const int veteranCommanderBonus = hero.hasActivePerk(COMMAND_SKILL, VETERAN_COMMANDER_PERK)
		? VETERAN_COMMANDER_EFFICIENCY_BONUS_PERCENT : 0;
	return efficiencyPercent(hero) + std::clamp(divineMandateEfficiencyBonusPercent, 0, newHorizonsCommand::usesPerkRules(&hero) ? 60 : 15)
		+ veteranCommanderBonus;
}

int secondWindPercent(const CGHeroInstance & hero)
{
	return secondWindPercent(hero, 0);
}

int secondWindPercent(const CGHeroInstance & hero,
	int divineMandateEfficiencyBonusPercent)
{
	return secondWindPercent(hero, divineMandateEfficiencyBonusPercent, false);
}

int secondWindPercent(const CGHeroInstance & hero,
	int divineMandateEfficiencyBonusPercent, bool crownAndAltar)
{
	int64_t leadership = 0;
	if(const auto capacity = hero.getLeadershipCapacity())
		leadership = capacity->capacity;
	const int efficiency = leadershipEfficiencyPercent(hero, divineMandateEfficiencyBonusPercent);
	const double leadershipComponent = 0.015 * static_cast<double>(leadership) * efficiency / 100.0 * (crownAndAltar ? 1.2 : 1.0);
	return std::clamp(50 + static_cast<int>(std::lround(leadershipComponent)), 0, 100);
}

bool hasCombinedArms(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(COMMAND_SKILL, COMBINED_ARMS_PERK);
}

bool hasDoubleCommand(const CGHeroInstance *)
{
	return false;
}

bool hasBattlePlan(const CGHeroInstance *)
{
	return false;
}

double combinedArmsFocusFirePercent(int rangedDamagePercent, const CGHeroInstance & hero)
{
	if(!hasCombinedArms(&hero))
		return 0;
	return static_cast<double>(rangedDamagePercent) / 2.0;
}

double combinedArmsFlankPercent(const JsonNode & meleeDamageFormula, const CGHeroInstance & hero)
{
	return combinedArmsFlankPercent(meleeDamageFormula, hero, 0);
}

double combinedArmsFlankPercent(const JsonNode & meleeDamageFormula, const CGHeroInstance & hero, int divineMandateEfficiencyBonusPercent)
{
	if(!hasCombinedArms(&hero))
		return 0;
	const double attackFactor = static_cast<double>(hero.getPrimSkillLevel(PrimarySkill::ATTACK))
		* attributeEfficiencyPercent(hero, PrimarySkill::ATTACK,
			divineMandateEfficiencyBonusPercent, true) / 100.0;
	return coefficientMath::boundedCoefficientComponent(meleeDamageFormula["attack"].Float(), attackFactor) / 2.0;
}

}
