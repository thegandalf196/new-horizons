/* Hero level-up skill evaluation, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "../StdInc.h"
#include "HeroManager.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"

namespace NK2AI
{
int HeroManager::selectBestSkillIndex(const HeroPtr & heroPtr, const std::vector<SecondarySkill> & skills) const
{
	return bestLevelUpSkill(heroPtr, skills).first;
}

std::pair<int, float> HeroManager::bestLevelUpSkill(const HeroPtr & heroPtr, const std::vector<SecondarySkill> & skills) const
{
	const auto role = getHeroRoleOrDefault(heroPtr);
	const auto & evaluator = role == MAIN ? mainSkillsEvaluator : scoutSkillsEvaluator;
	const auto * hero = heroPtr.getUnverified();
	if(!hero)
		return {0, 0.0f};
	const auto & rules = hero->getPrimaryGrowthRules();
	const auto ownFactionSkill = newHorizonsHeroes::factionSkill(rules, hero->getFactionID());
	int result = 0;
	float resultScore = -100;

	for(int i = 0; i < skills.size(); i++)
	{
		auto score = evaluator.evaluateSecSkill(hero, skills[i]);
		// New Horizons heroes already begin with their faction skill. Keep the
		// identity meaningful when it is offered for a later rank: the generic
		// evaluator has no legacy probability entry for these skills and would
		// otherwise routinely prefer an unrelated skill. The authoritative query
		// still validates the final choice; this is only the AI's preference.
		if(ownFactionSkill && newHorizonsHeroes::isFactionSkillForFaction(
			rules, hero->getFactionID(), skills[i]))
			score += 1000.0f;
		else if(newHorizonsHeroes::isFactionSkill(rules, skills[i]))
			score -= 1000.0f;
		if(score > resultScore)
		{
			resultScore = score;
			result = i;
		}

		logAi->trace(
			"Hero %s is offered to learn %d with score %f",
			heroPtr.nameOrDefault(),
			skills[i].toEnum(),
			score);
	}

	return {result, resultScore};
}

}
