/*
 * PrimaryAndExperienceReward.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "PrimaryAndExperienceReward.h"
#include "Reward.h"
#include "../callback/IGameEventCallback.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsSwampSeer.h"
namespace Rewardable
{
void grantPrimaryAndExperience(IGameEventCallback & gameEvents, const Reward & reward,
	const CGHeroInstance & hero, std::optional<Obj> source)
{
	const auto studyBefore = newHorizonsHeroes::captureStudyPrimarySnapshot(hero, source);
	for(size_t i = 0; i < reward.primary.size(); ++i)
		if(reward.primary[i] != 0)
			gameEvents.changePrimSkill(&hero, PrimarySkill(static_cast<int>(i)), reward.primary[i], ChangeValueMode::RELATIVE);
	const auto gained = newHorizonsHeroes::acceptedStudyPrimaryPoints(hero, studyBefore);
	const auto experience = reward.calculateHeroExperienceForAcceptedReward(&hero, source, gained);
	if(experience)
		gameEvents.giveExperience(&hero, experience);
}
}
