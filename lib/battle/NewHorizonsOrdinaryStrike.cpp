/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsOrdinaryStrike.h"
#include "NewHorizonsCreatureBurn.h"
#include "NewHorizonsReactiveDefense.h"
#include "NewHorizonsCoordinatedMarks.h"
#include "NewHorizonsInfernoStrikeDebuffs.h"
#include "NewHorizonsGraveDust.h"
#include "NewHorizonsGraveDustStrike.h"
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
	if(newHorizonsGraveDust::isEffect(bonus)) return true;
	if(newHorizonsCreatureBurn::isBurn(bonus) || newHorizonsReactiveDefense::isIronHideSpent(bonus) || newHorizonsReactiveDefense::isSolidarityProtection(bonus)
		|| newHorizonsInfernoStrikeDebuffs::isEffect(bonus) || newHorizonsCoordinatedMarks::isMark(bonus) || newHorizonsSepulchralGrip::isEffect(bonus) || newHorizonsShieldAndSword::isProtection(bonus))
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
		&& (newHorizonsGraveDustStrike::eligible(battle, attack)
			|| newHorizonsCreatureBurn::eligible(battle, attack)
			|| (!attack.shooting && newHorizonsCreatureBurn::eligible(battle, attack.reverse()))
			|| newHorizonsReactiveDefense::ironHidePercent(battle, attack) > 0
			|| (!attack.shooting && newHorizonsReactiveDefense::ironHidePercent(battle, attack.reverse()) > 0)
			|| newHorizonsInfernoStrikeDebuffs::primaryHit(battle, attack)
			|| (!attack.shooting && newHorizonsInfernoStrikeDebuffs::primaryHit(battle, attack.reverse()))
			|| newHorizonsCoordinatedMarks::needsProjection(battle, attack) || (!attack.shooting && !attack.retaliation && newHorizonsShieldAndSword::eligible(battle, *attack.attacker))
			|| newHorizonsSepulchralGrip::primaryHit(battle, attack)
			|| newHorizonsHeroes::ordinaryCreatureStrikeBonus(battle, attack)
			|| (!attack.shooting && newHorizonsSepulchralGrip::primaryHit(battle, attack.reverse()))
			|| (!attack.shooting && newHorizonsHeroes::ordinaryCreatureStrikeBonus(battle, attack.reverse())));
}
EffectPlan plan(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, int64_t primaryDamage, int64_t primaryBodyHealthLoss)
{
	EffectPlan result;
	if(attack.attacker && attack.defender)
	{
		result.target = attack.defender->unitId();
		result.remove = newHorizonsCoordinatedMarks::consumed(battle, attack);
	}
	if(!attack.attacker || !attack.defender || !attack.attacker->alive()
		|| !attack.defender->alive() || attack.attacker->isGhost() || attack.defender->isGhost()
		|| attack.attacker->isTimeStopped() || attack.defender->isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
		return result;
	if(auto spent = newHorizonsReactiveDefense::ironHideSpending(battle, attack))
	{
		for(const auto & old : *attack.defender->getUnstackedBonuses(CSelector(newHorizonsReactiveDefense::isIronHideSpent)))
			result.remove.push_back(*old);
		result.add.push_back(std::move(*spent));
	}
	auto effect = primaryDamage > 0 ? newHorizonsHeroes::ordinaryCreatureStrikeBonus(battle, attack) : nullptr;
	if(!effect && primaryDamage > 0)
		effect = newHorizonsSepulchralGrip::primaryHit(battle, attack);
	if(!effect && primaryDamage > 0)
		effect = newHorizonsInfernoStrikeDebuffs::primaryHit(battle, attack);
	if(!effect)
		effect = newHorizonsCoordinatedMarks::consecratingShot(battle, attack);
	if(!effect)
	{
		const auto pending = newHorizonsCreatureBurn::plan(battle, attack, primaryBodyHealthLoss);
		result.remove.insert(result.remove.end(), pending.remove.begin(), pending.remove.end());
		result.add.insert(result.add.end(), pending.add.begin(), pending.add.end());
		return result;
	}
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
