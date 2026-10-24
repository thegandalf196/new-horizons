/*
 * RewardExperience.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "Reward.h"
#include "../entities/hero/NewHorizonsSwampSeer.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../GameLibrary.h"
#include "../entities/hero/CHeroHandler.h"
#include "../mapping/CMapHeader.h"
#include "../spells/NewHorizonsMagic.h"
#include "../callback/IGameInfoCallback.h"
namespace
{
TExpType ordinaryExperience(const Rewardable::Reward & reward, const CGHeroInstance * hero, bool includePrimaryExperienceBonus,
	std::optional<Obj> sourceObject)
{
	const auto specialty = hero && sourceObject
		&& (*sourceObject == Obj::TREE_OF_KNOWLEDGE || *sourceObject == Obj::LEARNING_STONE)
		? hero->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::NATURAL_PHILOSOPHER) : std::nullopt;
	const auto experienceFor = [&](TExpType base)
	{
		if(!specialty || base <= 0)
			return base;
		const auto extra = newHorizonsHeroes::druidSpecialtyExtra(base, specialty->knowledgeObjectExperiencePercent);
		return base + std::min(extra, std::numeric_limits<TExpType>::max() - base);
	};
	const auto addExperience = [&](TExpType left, TExpType right)
	{
		if(specialty)
		{
			if(right > 0 && left > std::numeric_limits<TExpType>::max() - right)
				return std::numeric_limits<TExpType>::max();
			if(right < 0 && left < std::numeric_limits<TExpType>::min() - right)
				return std::numeric_limits<TExpType>::min();
		}
		return left + right;
	};
	const bool historianBonus = includePrimaryExperienceBonus && reward.primaryExperienceReward && hero
		&& hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.historian");
	TExpType result = 0;
	if(reward.heroExperience > 0)
	{
		result = hero ? hero->calculateXp(experienceFor(reward.heroExperience), historianBonus ? 50 : 0) : reward.heroExperience;
	}

	if(!hero)
		return result;
	if(reward.heroLevel > 0 && !reward.scalesPrimaryLevelExperience(hero))
		result = addExperience(result,
			LIBRARY->heroh->reqExp(hero->level + reward.heroLevel) - LIBRARY->heroh->reqExp(hero->level));

	ui32 maximumLevel = LIBRARY->heroh->maxSupportedLevel();
	if(hero->cb)
	{
		if(const auto * mapHeader = hero->cb->getMapHeader(); mapHeader && mapHeader->levelLimit > 0)
			maximumLevel = std::min<ui32>(maximumLevel, mapHeader->levelLimit);
	}

	if(hero->level >= maximumLevel)
		return result;

	if(reward.heroLevel > 0 && reward.scalesPrimaryLevelExperience(hero))
	{
		// Keep legacy level rewards unscaled. Primary Experience objects use
		// the same Learning/Historian composition as fixed Experience rewards.
		const ui32 targetLevel = std::min<uint64_t>(maximumLevel,
			static_cast<uint64_t>(hero->level) + reward.heroLevel);
		const TExpType levelExperience = LIBRARY->heroh->reqExp(targetLevel) - LIBRARY->heroh->reqExp(hero->level);
		result = addExperience(result, hero->calculateXp(experienceFor(levelExperience), historianBonus ? 50 : 0));
	}

	if(reward.heroExperienceNextLevelPercent <= 0)
		return result;

	const TExpType nextLevelExperience = LIBRARY->heroh->reqExp(hero->level + 1);
	const TExpType missingExperience = nextLevelExperience > hero->exp ? nextLevelExperience - hero->exp : 0;
	const TExpType percent = reward.heroExperienceNextLevelPercent;
	const TExpType percentageExperience = (missingExperience / 100) * percent
		+ (missingExperience % 100) * percent / 100;

	return addExperience(result, hero->calculateXp(experienceFor(percentageExperience), historianBonus ? 50 : 0));
}

}
bool Rewardable::Reward::scalesPrimaryLevelExperience(const CGHeroInstance * hero) const
{
	return hero && primaryExperienceReward && newHorizonsMagic::rulesActive(hero->getMagicRules());
}
TExpType Rewardable::Reward::calculateHeroExperience(const CGHeroInstance * hero, bool includePrimaryExperienceBonus,
	std::optional<Obj> sourceObject) const
{
	return newHorizonsHeroes::addStudyExperience(ordinaryExperience(*this, hero, includePrimaryExperienceBonus, sourceObject),
		newHorizonsHeroes::projectedSwampSeerStudyExperience(hero, *this, sourceObject));
}
TExpType Rewardable::Reward::calculateHeroExperienceForAcceptedReward(const CGHeroInstance * hero,
	std::optional<Obj> sourceObject, int64_t acceptedStudyPrimaryPoints) const
{
	return newHorizonsHeroes::addStudyExperience(ordinaryExperience(*this, hero, true, sourceObject),
		newHorizonsHeroes::swampSeerStudyExperience(hero, sourceObject, acceptedStudyPrimaryPoints));
}
