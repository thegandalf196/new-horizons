/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsOrdinaryStrike.h"
#include "NewHorizonsSepulchralGrip.h"
#include "NewHorizonsShieldAndSword.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../entities/hero/NewHorizonsAdditionalSpecialties.h"
#include "../entities/hero/CHero.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsOrdinaryStrike
{
bool isStrikeEffect(const Bonus * bonus)
{
	if(newHorizonsSepulchralGrip::isEffect(bonus) || newHorizonsShieldAndSword::isProtection(bonus))
		return true;
	if(!bonus || bonus->source != BonusSource::HERO_SPECIAL
		|| bonus->duration != BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		|| !vstd::contains(bonus->statusTags, BonusStatusTag::DEBUFF)
		|| !vstd::contains(bonus->statusTags, BonusStatusTag::NON_TRANSFERABLE))
		return false;
	return (bonus->type == BonusType::STACKS_INITIATIVE && bonus->statusIdentity == "wingbeat"
		&& bonus->val < 0 && bonus->sid == BonusSourceID(HeroTypeID(HeroTypeID::decode("core:aeris"))))
		|| (bonus->type == BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION
			&& bonus->statusIdentity == "measuredVolley" && bonus->val > 0
			&& bonus->sid == BonusSourceID(HeroTypeID(HeroTypeID::decode("core:theodorus"))));
}
bool isInitiativeEffect(const Bonus * bonus)
{
	return isStrikeEffect(bonus) && bonus->type == BonusType::STACKS_INITIATIVE;
}
bool mayChangeInitiative(const CBattleInfoCallback & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battle.battleGetFightingHero(side);
		if(hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == "core:aeris"
			&& newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero)
			&& hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"]["wingbeatInitiativePercent"].Integer() > 0)
			return true;
	}
	for(const auto * unit : battle.battleGetUnitsIf([](const battle::Unit * unit) { return unit != nullptr; }))
		if(unit && unit->hasBonus(CSelector(isInitiativeEffect)))
			return true;
	return false;
}

bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return attack.attacker && attack.defender
		&& ((!attack.shooting && !attack.retaliation && newHorizonsShieldAndSword::eligible(battle, *attack.attacker))
			|| newHorizonsSepulchralGrip::primaryHit(battle, attack)
			|| newHorizonsHeroes::ordinaryCreatureStrikeBonus(battle, attack)
			|| (!attack.shooting && newHorizonsSepulchralGrip::primaryHit(battle, attack.reverse()))
			|| (!attack.shooting && newHorizonsHeroes::ordinaryCreatureStrikeBonus(battle, attack.reverse())));
}
EffectPlan plan(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, int64_t primaryDamage)
{
	EffectPlan result;
	if(primaryDamage <= 0 || !attack.attacker || !attack.defender || !attack.attacker->alive()
		|| !attack.defender->alive() || attack.attacker->isGhost() || attack.defender->isGhost()
		|| attack.attacker->isTimeStopped() || attack.defender->isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
		return result;
	auto effect = newHorizonsHeroes::ordinaryCreatureStrikeBonus(battle, attack);
	if(!effect)
		effect = newHorizonsSepulchralGrip::primaryHit(battle, attack);
	if(!effect)
		return result;
	result.target = attack.defender->unitId();
	// Match only this specialty's explicit local status. No unrelated source,
	// spell, Initiative modifier or MDR contributor is removed or rewritten.
	const CSelector identity([&effect](const Bonus * existing)
	{
		return isStrikeEffect(existing) && existing->source == effect->source && existing->sid == effect->sid
			&& existing->type == effect->type && existing->statusIdentity == effect->statusIdentity;
	});
	for(const auto & existing : *attack.defender->getUnstackedBonuses(identity))
	{
		result.remove.push_back(*existing);
		if(std::abs(static_cast<int64_t>(existing->val)) > std::abs(static_cast<int64_t>(effect->val)))
		{
			effect->val = existing->val;
			effect->description = existing->description;
		}
	}
	result.add.push_back(*effect);
	return result;
}
}
