/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "queries/HeroLevelUpOffer.h"
#include "CGameHandler.h"
#include "../lib/callback/GameRandomizer.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>

HeroLevelUp newHorizonsLevelUp::prepareLevelUp(CGameHandler & handler, const CGHeroInstance * hero)
{
	const bool artilleryExpertBeforeGain = hero->getSecSkillLevel(SecondarySkill::ARTILLERY) == MasteryLevel::EXPERT;
	const bool logisticsExpertBeforeGain = hero->getSecSkillLevel(SecondarySkill::LOGISTICS) == MasteryLevel::EXPERT
		&& newHorizonsHeroes::masteryOptions(hero->getMasteryState().rules, SecondarySkill::LOGISTICS).has_value();
	// give primary skill
	logGlobal->trace("%s got level %d", hero->getNameTextID(), hero->level);
	auto gains = handler.randomizer->rollPrimarySkillsForLevelup(hero);
	const auto primarySkill = PrimarySkill(std::distance(gains.begin(), std::max_element(gains.begin(), gains.end())));
	for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
	{
		const auto before = hero->getBasePrimarySkillValue(PrimarySkill(i));
		if(gains[i])
		{
			SetPrimarySkill sps;
			sps.id = hero->id;
			sps.which = PrimarySkill(i);
			sps.mode = ChangeValueMode::RELATIVE;
			sps.val = gains[i];
			handler.sendAndApply(sps);
		}
		gains[i] = hero->getBasePrimarySkillValue(PrimarySkill(i)) - before;
	}

	HeroLevelUp hlu;
	hlu.player = hero->tempOwner;
	hlu.heroId = hero->id;
	hlu.primskill = primarySkill;
	hlu.primaryGains = gains;
	hlu.artilleryExpertBeforeGain = artilleryExpertBeforeGain;
	hlu.logisticsExpertBeforeGain = logisticsExpertBeforeGain;
	prepareChoices(handler, hero, hlu);

	const int reachedLevel = hero->level + 1;
	if(newHorizonsHeroes::usesPersistentHeroProgression(hero->getPerkState().rules))
		hlu.reachedLevel = reachedLevel;
	if(newHorizonsHeroes::quickStudyRerollInterval(hero->getPerkState().rules))
	{
		hlu.reachedLevel = reachedLevel;
		hlu.optionalRerollAvailable = hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.connoisseur")
			&& (!hlu.skills.empty() || !hlu.fourOffers.empty());
	}

	return hlu;
}
