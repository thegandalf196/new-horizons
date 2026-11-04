/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneInspiration.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>

namespace arcaneInspiration
{
int32_t corePercent(int32_t rank, bool grandInspiration)
{
	return rank > 0 ? 5 * std::clamp(rank, 0, 3) + (grandInspiration ? 10 : 0) : 0;
}
int32_t effectiveCorePercent(const CGHeroInstance * hero, int32_t basePercent)
{
	return basePercent > 0 ? basePercent + (hero && hero->hasActivePerk(SKILL,
		"new-horizons:warcasting.grandInspiration") ? 10 : 0) : 0;
}
Profile profile(const CGHeroInstance * hero)
{
	Profile result;
	if(!hero)
		return result;
	const int rank = std::clamp(hero->getPerkSkillRank(SKILL), 0, 3);
	const auto has = [hero](const char * perk) { return hero->hasActivePerk(SKILL, std::string(SKILL) + "." + perk); };
	result.corePercent = corePercent(rank, false);
	if(result.corePercent == 0)
		return result;
	for(const auto & [name, rider] : std::initializer_list<std::pair<const char *, Rider>>{
		{"arcaneValor", VALOR}, {"battleFocus", FOCUS}, {"arcaneBulwark", BULWARK},
		{"enchantedWeapons", WEAPONS}, {"lingeringInspiration", LINGERING},
		{"arcaneReprisal", REPRISAL}, {"arcaneSupremacy", SUPREMACY}})
		if(has(name))
			result.riders |= rider;
	result.spellshock = has("spellshock");
	return result;
}
Profile activeProfile(const battle::Unit & unit, PlayerColor owner)
{
	Profile result;
	const auto effects = unit.getBonuses(CSelector(isActive));
	for(const auto & effect : *effects)
		if(effect->spellCasterOwner == owner)
		{
			result.corePercent = std::max(result.corePercent, effect->val);
			result.riders |= static_cast<uint32_t>(effect->subtype.getNum());
		}
	return result;
}
int32_t retainedPercent(const battle::Unit & unit, PlayerColor owner)
{
	int32_t result = 0;
	const auto effects = unit.getBonuses(CSelector(isRetained));
	for(const auto & effect : *effects)
		if(effect->spellCasterOwner == owner)
			result = std::max(result, effect->val);
	return result;
}
AttackPlan attackPlan(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	AttackPlan result;
	if(!attack.attacker || !attack.defender)
		return result;
	if(newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker))
	{
		const auto owner = battle.battleGetOwner(attack.attacker);
		const auto policy = activeProfile(*attack.attacker, owner);
		result.damageBonusPercent = std::max(policy.corePercent, retainedPercent(*attack.attacker, owner));
		result.damageBonusPercent = effectiveCorePercent(battle.battleGetOwnerHero(attack.attacker), result.damageBonusPercent);
		if(policy.corePercent > 0)
		{
			if(attack.retaliation && (policy.riders & FOCUS))
				result.damageBonusPercent += 15;
			if(policy.riders & WEAPONS)
				result.defenseIgnorePercent = 15;
			if(policy.riders & SUPREMACY)
				result.protectionIgnorePercent = 20;
		}
		if(attack.attacker->hasBonus(CSelector(isSpellshock)))
			result.outputPenaltyPercent = 10;
	}
	if(attack.physicalDamage && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.defender))
	{
		const auto defender = activeProfile(*attack.defender, battle.battleGetOwner(attack.defender));
		if(defender.corePercent > 0 && (defender.riders & BULWARK))
			result.physicalReductionPercent = 10;
	}
	return result;
}
int64_t reprisalRawDamage(int64_t inflictedDamage)
{
	return std::max<int64_t>(0, inflictedDamage) / 10;
}
}
