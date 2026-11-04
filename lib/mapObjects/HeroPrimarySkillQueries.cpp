/*
 * HeroPrimarySkillQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../bonuses/PrimarySkillPolicy.h"
#include <algorithm>
#include <limits>

int CGHeroInstance::getPrimSkillLevel(PrimarySkill id) const
{
	const int ordinary = usesPrimaryGrowth()
		? primarySkillPolicy::growthValue(valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(id)), primaryGrowthRules)
		: primarySkills.getSkill(id).load();
	const int sage = id == PrimarySkill::KNOWLEDGE
		&& hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.sage") ? 50 : 0;
	return static_cast<int>(std::min<int64_t>(std::numeric_limits<int>::max(), static_cast<int64_t>(ordinary) + sage));
}
