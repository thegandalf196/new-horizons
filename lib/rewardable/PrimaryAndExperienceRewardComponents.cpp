/*
 * PrimaryAndExperienceRewardComponents.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "PrimaryAndExperienceReward.h"
#include "Reward.h"
#include "../networkPacks/Component.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsSwampSeer.h"
#include "../GameLibrary.h"
#include "../entities/hero/CHeroHandler.h"
#include <algorithm>
#include <limits>
namespace Rewardable
{
void appendExperienceComponents(std::vector<Component> & comps, const Reward & reward,
	const CGHeroInstance * h, std::optional<Obj> sourceObject)
{
	const auto studyExperience = newHorizonsHeroes::projectedSwampSeerStudyExperience(h, reward, sourceObject);
	if (reward.heroExperience || reward.heroExperienceNextLevelPercent || (reward.scalesPrimaryLevelExperience(h) && reward.heroLevel > 0))
	{
		TExpType experience = h ? reward.calculateHeroExperience(h, true, sourceObject) : reward.heroExperience;
		// Unclassified level rewards retain their separate legacy Level component.
		if(h && !reward.scalesPrimaryLevelExperience(h) && reward.heroLevel > 0)
			experience -= LIBRARY->heroh->reqExp(h->level + reward.heroLevel) - LIBRARY->heroh->reqExp(h->level);
		// Negative fixed Experience rewards were historically previewed even though
		// grantRewardBeforeLevelup only grants positive fixed Experience.
		if(h && reward.heroExperience < 0 && reward.heroExperienceNextLevelPercent == 0 && studyExperience == 0)
			experience = h->calculateXp(reward.heroExperience);
		const si32 componentExperience = static_cast<si32>(std::clamp<TExpType>(experience,
			std::numeric_limits<si32>::min(), std::numeric_limits<si32>::max()));
		comps.emplace_back(ComponentType::EXPERIENCE, componentExperience);
	}

	if (reward.heroLevel && !(reward.scalesPrimaryLevelExperience(h) && reward.heroLevel > 0))
		comps.emplace_back(ComponentType::LEVEL, reward.heroLevel);

}
void appendPrimaryStudyComponents(std::vector<Component> & comps, const Reward & reward,
	const CGHeroInstance * h, std::optional<Obj> sourceObject)
{
	const auto studyExperience = newHorizonsHeroes::projectedSwampSeerStudyExperience(h, reward, sourceObject);
	for (size_t i=0; i<reward.primary.size(); i++)
	{
		if (reward.primary[i] != 0)
		{
			const PrimarySkill skill(static_cast<int>(i));
			auto amount = reward.primary[i];
			if(h && newHorizonsHeroes::isSwampSeerStudyObject(sourceObject) && newHorizonsHeroes::swampSeerRules(h))
			{
				const auto change = h->projectPrimarySkillChange(skill, amount, ChangeValueMode::RELATIVE);
				amount = change.after - change.before;
			}
			comps.emplace_back(ComponentType::PRIM_SKILL, skill, amount);
		}
	}

	if(studyExperience != 0 && reward.heroExperience == 0 && reward.heroExperienceNextLevelPercent == 0
		&& !(reward.scalesPrimaryLevelExperience(h) && reward.heroLevel > 0))
		comps.emplace_back(ComponentType::EXPERIENCE, static_cast<si32>(std::clamp<TExpType>(studyExperience,
			std::numeric_limits<si32>::min(), std::numeric_limits<si32>::max())));

}
}
