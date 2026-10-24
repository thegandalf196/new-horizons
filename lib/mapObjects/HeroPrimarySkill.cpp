/*
 * HeroPrimarySkill.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../entities/hero/NewHorizonsPrimaryProfile.h"
#include "../bonuses/BonusList.h"
#include <algorithm>
newHorizonsHeroes::PrimarySkillChange CGHeroInstance::projectPrimarySkillChange(
	PrimarySkill primarySkill, si64 value, ChangeValueMode mode) const
{
	const auto skill = getFirstBonus(Selector::type()(BonusType::PRIMARY_SKILL)
		.And(Selector::subtype()(BonusSubtypeID(primarySkill)))
		.And(Selector::sourceType()(BonusSource::HERO_BASE_SKILL)));
	assert(skill);
	const auto before = skill->val;
	if(usesPrimaryGrowth())
	{
		const int64_t maximum = getPrimaryGrowthRules()["maxPrimary"].Integer();
		const auto next = mode == ChangeValueMode::ABSOLUTE ? value
			: before + std::clamp<int64_t>(value, -maximum, maximum);
		return {before, static_cast<int32_t>(std::clamp<int64_t>(next, 0, maximum))};
	}
	return {before, mode == ChangeValueMode::ABSOLUTE ? static_cast<si32>(value)
		: before + static_cast<si32>(value)};
}
void CGHeroInstance::setPrimarySkill(PrimarySkill primarySkill, si64 value, ChangeValueMode mode)
{
	auto skill = getLocalBonus(Selector::type()(BonusType::PRIMARY_SKILL)
		.And(Selector::subtype()(BonusSubtypeID(primarySkill)))
		.And(Selector::sourceType()(BonusSource::HERO_BASE_SKILL)));
	assert(skill);
	skill->val = projectPrimarySkillChange(primarySkill, value, mode).after;
	nodeHasChanged();
}
