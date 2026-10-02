/*
 * NewHorizonsDiscipline.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsDiscipline.h"

#include "../mapObjects/CGHeroInstance.h"

namespace
{
BonusSourceID disciplineSkillSource()
{
	static const BonusSourceID source{SecondarySkill{SecondarySkill::decode(
		std::string(newHorizonsDiscipline::SKILL))}};
	return source;
}
}

namespace newHorizonsDiscipline
{
bool hasSteadfast(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(STEADFAST));
}

bool hasHoldFast(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(HOLD_FAST));
}

bool hasFearless(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(FEARLESS));
}

Bonus holdFastMoraleFloorBonus()
{
	Bonus bonus(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::MINIMUM_MORALE,
		BonusSource::SECONDARY_SKILL, 0, disciplineSkillSource());
	bonus.valType = BonusValueType::INDEPENDENT_MAX;
	bonus.description.appendRawString("Hold Fast");
	return bonus;
}

bool isHoldFastMoraleFloorBonus(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		&& bonus->type == BonusType::MINIMUM_MORALE && bonus->val == 0
		&& bonus->valType == BonusValueType::INDEPENDENT_MAX
		&& bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->sid == disciplineSkillSource();
}

CSelector holdFastMoraleFloorBonusSelector()
{
	return CSelector([](const Bonus * bonus)
	{
		return isHoldFastMoraleFloorBonus(bonus);
	});
}
}
