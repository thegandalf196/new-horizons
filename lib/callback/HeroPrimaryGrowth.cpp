/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameRandomizer.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/CHeroClass.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsGenieSage.h"
#include <vstd/RNG.h>

std::array<int, GameConstants::PRIMARY_SKILLS> GameRandomizer::rollPrimarySkillsForLevelup(const CGHeroInstance * hero)
{
	if(!hero->usesPrimaryGrowth())
	{
		std::array<int, GameConstants::PRIMARY_SKILLS> gains{};
		++gains[rollPrimarySkillForLevelup(hero).getNum()];
		return gains;
	}
	const auto view = hero->getPrimaryGrowthView();
	if(!view)
		return {};

	if(view->extraGrowth.empty())
		return newHorizonsHeroes::completeSageGrowth(*hero, view->profile.growth);
	if(!heroSkillSeed.count(hero->getHeroTypeID()))
		heroSkillSeed.try_emplace(hero->getHeroTypeID(), getDefault().nextInt());
	auto & generator = heroSkillSeed.at(hero->getHeroTypeID()).seed;
	std::vector<newHorizonsHeroes::ExtraPrimaryRoll> opportunities;
	std::vector<int> draws;
	for(const auto & chance : view->extraGrowth)
	{
		opportunities.push_back({chance.attribute, chance.chancePercent});
		draws.push_back(generator.nextInt(0, 99));
	}
	return newHorizonsHeroes::completeSageGrowth(*hero,
		newHorizonsHeroes::calculatePrimaryGrowth(view->profile, opportunities, draws));
}

PrimarySkill GameRandomizer::rollPrimarySkillForLevelup(const CGHeroInstance * hero)
{
	if(!heroSkillSeed.count(hero->getHeroTypeID()))
		heroSkillSeed.try_emplace(hero->getHeroTypeID(), getDefault().nextInt());

	const bool isLowLevelHero = hero->level < GameConstants::HERO_HIGH_LEVEL;
	const auto & skillChances = isLowLevelHero ? hero->getHeroClass()->primarySkillLowLevel : hero->getHeroClass()->primarySkillHighLevel;
	auto & heroRng = heroSkillSeed.at(hero->getHeroTypeID());

	if(hero->isCampaignYog())
	{
		// Yog can only receive Attack or Defence on level-up
		std::vector<int> yogChances = {skillChances[0], skillChances[1]};
		return static_cast<PrimarySkill>(RandomGeneratorUtil::nextItemWeighted(yogChances, heroRng.seed));
	}
	return static_cast<PrimarySkill>(RandomGeneratorUtil::nextItemWeighted(skillChances, heroRng.seed));
}
