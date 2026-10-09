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
constexpr std::string_view SHADOW_ASSAULT_ATTACKER_STACKING_KEY =
	"new-horizons:shroudOfMalassa.shadowAssaultSpent.attacker";
constexpr std::string_view SHADOW_ASSAULT_DEFENDER_STACKING_KEY =
	"new-horizons:shroudOfMalassa.shadowAssaultSpent.defender";

bool isValidShadowAssaultSide(BattleSide side)
{
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
}

std::string_view shadowAssaultStackingKey(BattleSide side)
{
	switch(side)
	{
		case BattleSide::ATTACKER: return SHADOW_ASSAULT_ATTACKER_STACKING_KEY;
		case BattleSide::DEFENDER: return SHADOW_ASSAULT_DEFENDER_STACKING_KEY;
		default: throw std::invalid_argument("Shadow Assault marker requires an attacking battle side");
	}
}

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

bool hasVeiledMovement(const CGHeroInstance * hero)
{
	// hasActivePerk reads the hero's saved registry, not installed defaults.
	// Rank additionally requires the Ghost Walk capability itself to be active.
	return rank(hero) > 0 && hero->hasActivePerk(std::string(SKILL_ID),
		std::string(VEILED_MOVEMENT_PERK_ID));
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

bool hasDeepFlank(const CGHeroInstance * hero)
{
	return rank(hero) >= 2 && hero->hasActivePerk(std::string(SKILL_ID), std::string(DEEP_FLANK_PERK_ID));
}

double deepFlankDamagePercent(int value)
{
	return flankingDamagePercent(value) / 2.0;
}

bool hasEncircledDoom(const CGHeroInstance * hero)
{
	return rank(hero) == 3 && hero->hasActivePerk(std::string(SKILL_ID), std::string(ENCIRCLED_DOOM_PERK_ID));
}

int encircledDoomDamagePercent(int distinctSides)
{
	return std::max(0, std::clamp(distinctSides, 0, 6) - 1) * 10;
}

bool hasVanish(const CGHeroInstance * hero)
{
	return rank(hero) == 3 && hero->hasActivePerk(std::string(SKILL_ID), std::string(VANISH_PERK_ID));
}

int vanishMovementAllowance(int speed)
{
	return std::max(0, speed) / 2;
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

bool hasShadowAssault(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(SHADOW_ASSAULT_PERK_ID));
}

Bonus shadowAssaultSpentMarker(BattleSide attackingSide)
{
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::SECONDARY_SKILL,
		0, shroudSkillSource());
	bonus.stacking = std::string(shadowAssaultStackingKey(attackingSide));
	bonus.hidden = true;
	return bonus;
}

bool isShadowAssaultSpentMarker(const Bonus * bonus, BattleSide attackingSide)
{
	return bonus && isValidShadowAssaultSide(attackingSide)
		&& bonus->duration == BonusDuration::ONE_BATTLE && bonus->type == BonusType::NONE
		&& bonus->val == 0 && bonus->source == BonusSource::SECONDARY_SKILL
		&& bonus->sid == shroudSkillSource()
		&& bonus->stacking == shadowAssaultStackingKey(attackingSide);
}

int shadowAssaultDefenseIgnorePercent(const CGHeroInstance * hero, const battle::Unit * target,
	BattleSide attackingSide)
{
	if(!hasShadowAssault(hero) || !target || !isValidShadowAssaultSide(attackingSide))
		return 0;
	const bool alreadySpent = target->hasBonus(CSelector([attackingSide](const Bonus * bonus)
	{
		return isShadowAssaultSpentMarker(bonus, attackingSide);
	}));
	return alreadySpent ? 0 : SHADOW_ASSAULT_DEFENSE_IGNORE_PERCENT;
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

bool hasNightProwler(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SKILL_ID), std::string(NIGHT_PROWLER_PERK_ID));
}

std::vector<Bonus> nightProwlerDamageBonuses()
{
	std::vector<Bonus> result;
	result.reserve(2);
	const auto duration = BonusDuration::STACK_ACTIVATION | BonusDuration::UNTIL_ATTACK;

	Bonus meleeDamage(duration, BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSource::SECONDARY_SKILL,
		NIGHT_PROWLER_DAMAGE_PERCENT, shroudSkillSource(), BonusCustomSubtype::damageTypeMelee);
	meleeDamage.stacking = std::string(NIGHT_PROWLER_MELEE_STACKING_KEY);
	meleeDamage.description.appendRawString("Night Prowler: +10% damage on the next attack this activation");
	result.push_back(std::move(meleeDamage));

	Bonus rangedDamage(duration, BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSource::SECONDARY_SKILL,
		NIGHT_PROWLER_DAMAGE_PERCENT, shroudSkillSource(), BonusCustomSubtype::damageTypeRanged);
	rangedDamage.stacking = std::string(NIGHT_PROWLER_RANGED_STACKING_KEY);
	rangedDamage.description.appendRawString("Night Prowler: +10% damage on the next attack this activation");
	result.push_back(std::move(rangedDamage));
	return result;
}

bool isNightProwlerBonus(const Bonus * bonus)
{
	if(!bonus || bonus->duration != (BonusDuration::STACK_ACTIVATION | BonusDuration::UNTIL_ATTACK)
		|| bonus->type != BonusType::PERCENTAGE_DAMAGE_BOOST
		|| bonus->val != NIGHT_PROWLER_DAMAGE_PERCENT
		|| bonus->source != BonusSource::SECONDARY_SKILL || bonus->sid != shroudSkillSource())
		return false;

	const bool melee = bonus->subtype == BonusCustomSubtype::damageTypeMelee
		&& bonus->stacking == NIGHT_PROWLER_MELEE_STACKING_KEY;
	const bool ranged = bonus->subtype == BonusCustomSubtype::damageTypeRanged
		&& bonus->stacking == NIGHT_PROWLER_RANGED_STACKING_KEY;
	return melee || ranged;
}

bool deniesRetaliation(int value)
{
	return value >= 3;
}
}
