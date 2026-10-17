/*
 * HeroPrimarySkillQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../bonuses/PrimarySkillPolicy.h"

int CGHeroInstance::getPrimSkillLevel(PrimarySkill id) const
{
	if(usesPrimaryGrowth())
		return primarySkillPolicy::growthValue(valOfBonuses(BonusType::PRIMARY_SKILL, BonusSubtypeID(id)), primaryGrowthRules);
	return primarySkills.getSkill(id);
}
