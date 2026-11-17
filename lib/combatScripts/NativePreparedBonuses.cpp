/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativePreparedBonuses.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusCache.h"
#include <algorithm>
#include <limits>

namespace nativeDamage
{
int ordinaryStatWithWarfare(int ordinary, int warfare)
{
	return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(ordinary) + warfare,
		0, std::numeric_limits<int>::max()));
}

CSelector preparedRangeSelector(BonusType type, bool shooting)
{
	const auto range = shooting ? BonusLimitEffect::ONLY_DISTANCE_FIGHT : BonusLimitEffect::ONLY_MELEE_FIGHT;
	return Selector::type()(type).And(Selector::effectRange()(BonusLimitEffect::NO_LIMIT)
		.Or(Selector::effectRange()(range)));
}

std::optional<PreparedUnit> prepareOwnedBonuses(const BonusList & effective,
	PreparedUnit result, int warfare, bool shooting, const std::bitset<bonusTypeCount> * dirty)
{
	auto & facts = result.unit;
	const auto changed = [&](BonusType type) { return !dirty || (*dirty)[static_cast<size_t>(type)]; };
	if(!dirty)
	{
		facts.bonuses.reset();
		facts.values.fill(0);
	}
	else facts.bonuses &= ~*dirty;
	for(const auto & bonus : effective)
	{
		const auto index = static_cast<size_t>(bonus->type);
		if(index < bonusTypeCount && (!dirty || (*dirty)[index])) facts.bonuses[index] = true;
	}
	for(const auto type : {BonusType::SIEGE_WEAPON, BonusType::HATE, BonusType::HATES_TRAIT,
		BonusType::SLAYER, BonusType::REVENGE, BonusType::BONUS_DAMAGE_PERCENTAGE})
		if(facts.has(type)) return {};
	result.shooting = shooting;
	for(const auto type : {BonusType::ALWAYS_MAXIMUM_DAMAGE, BonusType::ALWAYS_MINIMUM_DAMAGE,
		BonusType::IN_FRENZY, BonusType::KING, BonusType::GENERAL_DAMAGE_PREMY,
		BonusType::JOUSTING, BonusType::FORGETFULL, BonusType::LEVEL_SPELL_IMMUNITY,
		BonusType::VULNERABLE_FROM_BACK, BonusType::DAMAGE_RECEIVED_CAP,
		BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE})
		if(changed(type)) facts.values[static_cast<size_t>(type)] = facts.has(type) ? effective.valOfBonuses(Selector::type()(type)) : 0;
	const auto & selectors = UnitBonusValuesProxy::canonicalSelectors();
	const auto amount = [&](UnitBonusValuesProxy::ECacheKeys key) { return effective.valOfBonuses(selectors[key]); };
	if(changed(BonusType::CREATURE_DAMAGE))
	{
		facts.minDamage = amount(shooting ? UnitBonusValuesProxy::MIN_DAMAGE_RANGED : UnitBonusValuesProxy::MIN_DAMAGE_MELEE);
		facts.maxDamage = amount(shooting ? UnitBonusValuesProxy::MAX_DAMAGE_RANGED : UnitBonusValuesProxy::MAX_DAMAGE_MELEE);
	}
	if(facts.minDamage > facts.maxDamage) return {};
	if(changed(BonusType::PRIMARY_SKILL))
	{
		facts.attack = ordinaryStatWithWarfare(amount(shooting ? UnitBonusValuesProxy::ATTACK_RANGED : UnitBonusValuesProxy::ATTACK_MELEE), warfare);
		facts.defense = ordinaryStatWithWarfare(amount(shooting ? UnitBonusValuesProxy::DEFENCE_RANGED : UnitBonusValuesProxy::DEFENCE_MELEE), warfare);
	}
	if(changed(BonusType::PHYSICAL_DEFENSE))
		result.physicalDefense = effective.valOfBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE));
	const auto rangedValue = [&](BonusType type)
	{
		return effective.valOfBonuses(preparedRangeSelector(type, shooting));
	};
	if(changed(BonusType::ENEMY_DEFENCE_REDUCTION)) result.enemyDefenseReduction = rangedValue(BonusType::ENEMY_DEFENCE_REDUCTION);
	if(changed(BonusType::ENEMY_ATTACK_REDUCTION)) result.enemyAttackReduction = rangedValue(BonusType::ENEMY_ATTACK_REDUCTION);
	if(changed(BonusType::GENERAL_ATTACK_REDUCTION)) result.attackReduction = rangedValue(BonusType::GENERAL_ATTACK_REDUCTION);
	if(changed(BonusType::PERCENTAGE_DAMAGE_BOOST)) result.damageBoost = effective.valOfBonuses(Selector::typeSubtype(BonusType::PERCENTAGE_DAMAGE_BOOST,
		shooting ? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee));
	if(changed(BonusType::GENERAL_DAMAGE_REDUCTION)) result.petrification = effective.valOfBonuses(Selector::typeSubtype(BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusCustomSubtype::damageTypeAll).And(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT)));
	return result;
}
}
