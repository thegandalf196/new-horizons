/* Connoisseur skill-card policy, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "../StdInc.h"
#include "HeroManager.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/SkillChoiceEligibility.h"
#include <cmath>
#include <limits>

namespace NK2AI
{
std::optional<size_t> HeroManager::connoisseurSkillCard(const HeroPtr & heroPtr,
	const std::vector<SecondarySkill> & skills, int reachedLevel) const
{
	const auto * hero = heroPtr.getUnverified();
	const auto player = cc->getPlayerID();
	if(!hero || !player || hero->getOwner() != *player || reachedLevel != hero->level || skills.empty())
		return std::nullopt;
	try
	{
		if(!newHorizonsHeroes::quickStudyRerollInterval(hero->getPerkState().rules)
			|| !hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.connoisseur"))
			return std::nullopt;
		size_t weakest = 0;
		float weakestScore = std::numeric_limits<float>::infinity();
		for(size_t i = 0; i < skills.size(); ++i)
		{
			if(!newHorizonsHeroes::isValidLevelUpSkillChoice(*cc, hero, skills[i]))
				return std::nullopt;
			const float score = bestLevelUpSkill(heroPtr, {skills[i]}).second;
			if(!std::isfinite(score))
				return std::nullopt;
			if(score < weakestScore)
			{
				weakest = i;
				weakestScore = score;
			}
		}
		const auto alternatives = newHorizonsHeroes::connoisseurSkillCandidates(*cc, *hero, skills);
		for(const auto skill : alternatives)
		{
			const float score = bestLevelUpSkill(heroPtr, {skill}).second;
			if(std::isfinite(score) && score > weakestScore)
				return weakest;
		}
	}
	catch(const std::exception &)
	{
		// Incomplete captured rules do not justify discarding a visible card.
	}
	return std::nullopt;
}
}
