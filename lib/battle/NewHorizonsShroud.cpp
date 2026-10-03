/*
 * NewHorizonsShroud.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsShroud.h"

#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
#include "Unit.h"

namespace
{
BonusSourceID shroudSkillSource()
{
	static const BonusSourceID source{SecondarySkill{SecondarySkill::decode(
		std::string(newHorizonsShroud::SKILL_ID))}};
	return source;
}
}

namespace newHorizonsShroud
{
int rank(const CGHeroInstance * hero)
{
	if(!hero)
		return 0;
	try
	{
		return std::clamp(hero->getPerkSkillRank(std::string(SKILL_ID)), 0, 3);
	}
	catch(const std::exception &)
	{
		return 0;
	}
}

int flankingDamagePercent(int value)
{
	switch(value)
	{
		case 1: return 25;
		case 2: return 40;
		case 3: return 60;
		default: return 0;
	}
}

int backstabDamagePercent(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(BACKSTAB_PERK_ID))
		? BACKSTAB_DAMAGE_PERCENT : 0;
}

bool hasAmbusher(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(AMBUSHER_PERK_ID));
}

Bonus ambusherSpentMarker()
{
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SECONDARY_SKILL,
		0, shroudSkillSource());
	bonus.stacking = std::string(AMBUSHER_STACKING_KEY);
	bonus.hidden = true;
	return bonus;
}

bool isAmbusherSpentMarker(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::ONE_BATTLE && bonus->type == BonusType::NONE
		&& bonus->val == 0 && bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->sid == shroudSkillSource() && bonus->stacking == AMBUSHER_STACKING_KEY;
}

int ambusherDamagePercent(const CGHeroInstance * hero, const battle::Unit * unit)
{
	if(!hasAmbusher(hero) || !unit)
		return 0;
	const bool alreadySpent = unit->hasBonus(CSelector([](const Bonus * bonus)
	{
		return isAmbusherSpentMarker(bonus);
	}));
	return alreadySpent ? 0 : AMBUSHER_DAMAGE_PERCENT;
}

bool hasNoEscape(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(NO_ESCAPE_PERK_ID));
}

bool hasEvasiveShroud(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(EVASIVE_SHROUD_PERK_ID));
}

Bonus noEscapeSpeedPenalty()
{
	Bonus bonus(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::STACKS_SPEED,
		BonusSource::SECONDARY_SKILL, NO_ESCAPE_SPEED_PENALTY, shroudSkillSource());
	bonus.stacking = std::string(NO_ESCAPE_STACKING_KEY);
	bonus.description.appendRawString("No Escape: -2 Speed until the next Creature Activation");
	return bonus;
}

bool isNoEscapeSpeedPenalty(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		&& bonus->type == BonusType::STACKS_SPEED && bonus->val == NO_ESCAPE_SPEED_PENALTY
		&& bonus->source == BonusSource::SECONDARY_SKILL && bonus->sid == shroudSkillSource()
		&& bonus->stacking == NO_ESCAPE_STACKING_KEY;
}

Bonus evasiveShroudProtection()
{
	Bonus bonus(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
		BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSource::SECONDARY_SKILL,
		EVASIVE_SHROUD_REDUCTION_BASIS_POINTS, shroudSkillSource());
	bonus.stacking = std::string(EVASIVE_SHROUD_STACKING_KEY);
	bonus.description.appendRawString("Evasive Shroud: 15% physical damage reduction until the next Creature Activation");
	return bonus;
}

bool isEvasiveShroudProtection(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		&& bonus->type == BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS
		&& bonus->val == EVASIVE_SHROUD_REDUCTION_BASIS_POINTS
		&& bonus->source == BonusSource::SECONDARY_SKILL && bonus->sid == shroudSkillSource()
		&& bonus->stacking == EVASIVE_SHROUD_STACKING_KEY;
}

bool deniesRetaliation(int value)
{
	return value >= 3;
}
}
