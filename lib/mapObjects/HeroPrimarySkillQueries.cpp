/*
 * HeroPrimarySkillQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../bonuses/PrimarySkillPolicy.h"
#include "../bonuses/BonusList.h"
#include "../battle/NewHorizonsElementalAttunement.h"
#include <algorithm>
#include <limits>

int CGHeroInstance::getPrimSkillLevel(PrimarySkill id) const
{
	int effective = valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(id));
	if(id == PrimarySkill::SPELL_POWER && newHorizonsElementalAttunement::perfectConvergence(this))
	{
		// Use ordinary Bonus value-mode stacking without attaching mutable derived
		// state to a saved hero or multiplying the spell damage a second time.
		BonusList primary(*getAllBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(id))));
		primary.push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
			BonusSource::SECONDARY_SKILL, 25, BonusSourceID(SecondarySkill(SecondarySkill::decode(newHorizonsElementalAttunement::SKILL_ID))),
			BonusSubtypeID(id), BonusValueType::PERCENT_TO_ALL));
		effective = primary.totalValue();
	}
	const int ordinary = usesPrimaryGrowth()
		? primarySkillPolicy::growthValue(effective, primaryGrowthRules)
		: primarySkills.getSkill(id).load();
	const int sage = id == PrimarySkill::KNOWLEDGE
		&& hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.sage") ? 50 : 0;
	return static_cast<int>(std::min<int64_t>(std::numeric_limits<int>::max(), static_cast<int64_t>(ordinary) + sage));
}
