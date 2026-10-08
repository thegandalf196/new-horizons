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
#include "../bonuses/BonusList.h"

#include <unordered_map>
#include <limits>
#include <algorithm>

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

bool hasEspritDeCorps(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL), std::string(ESPRIT_DE_CORPS));
}

TConstBonusListPtr espritDeCorpsMoraleBonuses(const CGHeroInstance * hero,
	const IBonusBearer & bearer, bool stackBonuses)
{
	const auto selector = Selector::type()(BonusType::MORALE);
	if(!hasEspritDeCorps(hero))
		return stackBonuses ? bearer.getBonuses(selector) : bearer.getUnstackedBonuses(selector);

	auto adjusted = std::make_shared<BonusList>();
	std::unordered_map<const Bonus *, std::shared_ptr<Bonus>> copies;
	const auto bonuses = bearer.getUnstackedBonuses(selector);
	for(const auto & bonus : *bonuses)
	{
		const bool hostile = bonus->appliedByEnemy
			|| (bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE && bonus->bonusOwner != hero->tempOwner);
		if(bonus->source == BonusSource::ARMY && bonus->val < 0 && !hostile)
		{
			auto & copy = copies[bonus.get()];
			if(!copy)
			{
				copy = std::make_shared<Bonus>(*bonus);
				++copy->val;
			}
			adjusted->push_back(copy);
		}
		else
			adjusted->push_back(bonus);
	}
	if(stackBonuses)
		adjusted->stackBonuses();
	return adjusted;
}

int32_t espritDeCorpsMoraleAdjustment(const CGHeroInstance * hero, const IBonusBearer & bearer)
{
	if(!hasEspritDeCorps(hero))
		return 0;
	const auto adjusted = espritDeCorpsMoraleBonuses(hero, bearer);
	const auto original = bearer.getBonusesOfType(BonusType::MORALE);
	const int64_t difference = static_cast<int64_t>(adjusted->totalValue()) - original->totalValue();
	return static_cast<int32_t>(std::clamp<int64_t>(difference,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
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
