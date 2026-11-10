/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "CommandSkillValue.h"
#include "HeroManager.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include <algorithm>

namespace NK2AI
{
float commandArmyUnlockValue(int64_t before, int64_t after)
{
	if(before < 1 || after <= before) return 0;
	float score = 0;
	for(int64_t milestone = 2; milestone <= 5; ++milestone)
		if(before < milestone && after >= milestone) score += milestone == 5 ? 2.0f : 1.0f;
	// Counting entitlements is arithmetic, even for a custom large rank bonus.
	score += static_cast<float>(std::max<int64_t>(0, after - std::max<int64_t>(before, 5))) * 0.5f;
	return score;
}

std::optional<float> commandClassSkillScore(const CGHeroInstance & hero, SecondarySkill skill, bool main)
{
	const auto rules = hero.getArmyCommandRules();
	if(!rules) return {};
	const auto weight = newHorizonsHeroes::skillOfferWeight(hero.getPrimaryGrowthRules(), skill);
	if(!weight) return {};
	// Captured class affinity supplies a bounded generic prior for current IDs.
	// Role-specific strategic overrides remain in HeroManager.
	float score = std::clamp(*weight, 0, 10) * (main ? 0.2f : 0.1f);
	if(SecondarySkill::encode(skill.getNum()) == "new-horizons:authority")
	{
		const int rank = std::clamp<int>(hero.getSecSkillLevel(skill), 0, 3);
		if(rank == 3) return score; // Learned Expert still contributes to the hero profile; no fourth-rank reward.
		const auto before = newHorizonsHeroes::commandValue(*rules, hero.level, rank).effective();
		const auto after = newHorizonsHeroes::commandValue(*rules, hero.level, rank + 1).effective();
		score += commandArmyUnlockValue(before, after) * (main ? 1.0f : 0.5f);
	}
	return score;
}

void CommandClassSkillRule::evaluateScore(const CGHeroInstance * hero, SecondarySkill skill, float & score) const
{
	if(const auto value = commandClassSkillScore(*hero, skill, mainHero))
		score = evaluateNewHorizonsStrategicSkillRoleScore(SecondarySkill::encode(skill.getNum()),
			mainHero ? HeroRole::MAIN : HeroRole::SCOUT).value_or(*value);
}
}
