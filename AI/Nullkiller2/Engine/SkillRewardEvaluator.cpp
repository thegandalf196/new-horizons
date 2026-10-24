/*
 * SkillRewardEvaluator.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "PriorityEvaluator.h"
#include "Nullkiller.h"
#include "../AIUtility.h"
#include "../Analyzers/HeroManager.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/rewardable/Interface.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/entities/hero/NewHorizonsSwampSeer.h"
#include "../../../lib/mapObjects/NewHorizonsAcademicStudy.h"
#include "../../../lib/mapObjects/NewHorizonsSage.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <vcmi/spells/Service.h>
#include <vcmi/spells/Spell.h>
#include <algorithm>
#include <cmath>
namespace NK2AI
{
namespace
{
float fixedStudyExperienceValue(const CGObjectInstance * target, const CGHeroInstance * hero)
{
	if(!hero || !newHorizonsHeroes::isSwampSeerStudyObject(target->ID) || target->wasVisited(hero))
		return 0;
	const auto * rewardable = dynamic_cast<const Rewardable::Interface *>(target);
	if(!rewardable)
		return 0;
	const auto rewards = rewardable->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT);
	if(rewards.empty())
		return 0;
	ui32 selected;
	if(rewardable->configuration.selectMode == Rewardable::SELECT_FIRST)
		selected = rewards.front();
	else if(target->ID == Obj::SCHOOL_OF_MAGIC && rewardable->configuration.selectMode == Rewardable::SELECT_PLAYER)
	{
		const auto & firstReward = rewardable->configuration.info.at(rewards.front()).reward;
		if(firstReward.getDisplayedComponent(hero, target->ID).type != ComponentType::PRIM_SKILL)
			return 0; // Preserve unrelated resource-choice heuristics on custom objects.
		selected = rewards.back(); // Existing AI dialog chooses last; do not value an unchosen better row.
	}
	else
		return 0; // No RNG, all-reward simulation or unrelated choice policy is introduced.
	const auto & reward = rewardable->configuration.info.at(selected).reward;
	const auto experience = newHorizonsHeroes::projectedSwampSeerStudyExperience(hero, reward, target->ID);
	return static_cast<float>(experience) / (1000.0f * std::sqrt(std::max(1u, hero->level)));
}
float configurableSkillReward(const CGObjectInstance * target, const CGHeroInstance * hero)
{
	auto rewardable = dynamic_cast<const Rewardable::Interface *>(target);

	if(rewardable)
	{
		auto totalValue = 0.0f;

		for(int index : rewardable->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT))
		{
			auto & info = rewardable->configuration.info[index];

			auto rewardValue = 0.0f;

			if(!info.reward.spells.empty())
			{
				for(auto spellID : info.reward.spells)
				{
					const spells::Spell * spell = LIBRARY->spells()->getById(spellID);

					if(hero->canLearnSpell(spell, true))
					{
						rewardValue += std::sqrt(hero->getSpellLevel(spell)) / 4.0f;
					}
				}

				totalValue += rewardValue / info.reward.spells.size();
			}

			if(!info.reward.primary.empty())
			{
				for(auto value : info.reward.primary)
				{
					totalValue += value;
				}
			}
		}

		return totalValue;
	}

	return 0;
}
}
float RewardEvaluator::evaluateWitchHutSkillScore(const CGObjectInstance * hut, const CGHeroInstance * hero, HeroRole role) const
{
	auto rewardable = dynamic_cast<const CRewardableObject *>(hut);
	assert(rewardable);

	auto skill = SecondarySkill(*rewardable->configuration.getVariable("secondarySkill", "gainedSkill"));

	// TODO: Mircea: Move to constants
	if(!hut->wasVisited(hero->tempOwner))
		return role == HeroRole::SCOUT ? 2 : 0;

	if(hero->getSecSkillLevel(skill) != MasteryLevel::NONE
		|| static_cast<int>(hero->secSkills.size()) >= aiNk->cc->getSettings().getInteger(EGameSettings::HEROES_SKILL_PER_HERO))
		return 0;

	auto score = aiNk->heroManager->evaluateSecSkill(skill, hero);

	// TODO: Mircea: Move to constants
	return score >= 2 ? (role == HeroRole::MAIN ? 10 : 4) : score;
}

float RewardEvaluator::getSkillReward(const CGObjectInstance * target, const CGHeroInstance * hero, HeroRole role) const
{
	const float enemyHeroEliminationSkillRewardRatio = 0.5f;

	if(!target)
		return 0;

	switch(target->ID)
	{
	case Obj::TOWN:
	{
		const auto * town = dynamic_cast<const CGTownInstance *>(target);
		if(!town || !hero)
			return 0;
		// Same units as an ordinary 1000-XP learning-stone benefit, including
		// the recipient's actual Learning composition, without capture duplication.
		float reward = static_cast<float>(newHorizonsLearning::academicStudyExperience(*hero, *town))
			/ (1000.0f * std::sqrt(hero->level));
		if(newHorizonsSage::firstGuildVisit(*hero, *town))
			if(const auto spell = newHorizonsSage::selectSpell(*hero, *town))
			{
				const bool reveals = newHorizonsSage::wisdomReveal(*hero, *town).has_value();
				const bool learns = newHorizonsSage::learningSelected(*hero);
				if(reveals || learns)
					reward += 0.5f * newHorizonsMagic::spellLevel(hero->getMagicRules(), *spell);
			}
		return reward;
	}
	case Obj::STAR_AXIS:
	case Obj::SCHOOL_OF_MAGIC:
	case Obj::GARDEN_OF_REVELATION:
		return 1 + fixedStudyExperienceValue(target, hero);
	case Obj::SCHOLAR:
	case Obj::SCHOOL_OF_WAR:
	case Obj::MARLETTO_TOWER:
	case Obj::MERCENARY_CAMP:
		return 1;
	case Obj::TREE_OF_KNOWLEDGE:
	case Obj::LEARNING_STONE:
	{
		const float basePriority = target->ID == Obj::TREE_OF_KNOWLEDGE ? 1.0f : 1.0f / std::sqrt(hero->level);
		const auto * rewardable = dynamic_cast<const Rewardable::Interface *>(target);
		if(!rewardable)
			return basePriority;

		float bestExperienceRatio = 1.0f;
		for(const auto index : rewardable->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT))
		{
			const auto & reward = rewardable->configuration.info.at(index).reward;
			if(!reward.primaryExperienceReward)
				continue;

			const TExpType actualExperience = reward.calculateHeroExperience(hero);
			const TExpType ordinaryExperience = reward.calculateHeroExperience(hero, false);
			if(ordinaryExperience <= 0)
				continue;

			bestExperienceRatio = std::max(bestExperienceRatio,
				static_cast<float>(actualExperience) / static_cast<float>(ordinaryExperience));
		}
		return basePriority * bestExperienceRatio;
	}
	case Obj::ARENA:
		return 2;
	case Obj::SHRINE_OF_MAGIC_INCANTATION:
		return 0.25f;
	case Obj::SHRINE_OF_MAGIC_GESTURE:
		return 1.0f;
	case Obj::SHRINE_OF_MAGIC_THOUGHT:
		return 2.0f;
	case Obj::LIBRARY_OF_ENLIGHTENMENT:
		return 8 + fixedStudyExperienceValue(target, hero);
	case Obj::WITCH_HUT:
		return evaluateWitchHutSkillScore(target, hero, role);
	case Obj::PANDORAS_BOX:
		//Can contains experience, spells, or skills (only on custom maps)
		return 2.5f;
	case Obj::HERO:
	{
		const auto * other = dynamic_cast<const CGHeroInstance *>(target);
		if(aiNk->cc->getPlayerRelations(target->tempOwner, aiNk->playerID) == PlayerRelations::ENEMIES)
			return enemyHeroEliminationSkillRewardRatio * other->level;
		return teachingMeetingReward(aiNk, hero, other);
	}

	default:
		break;
	}

	return configurableSkillReward(target, hero);
}


}
