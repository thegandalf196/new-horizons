/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeDamageFacts.h"
#include "IDamageCalculatorScript.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"

namespace nativeDamage
{
namespace
{
using Query = std::pair<CSelector, std::string>;

Query typeQuery(BonusType type)
{
	return {Selector::type()(type), "type_" + std::to_string(static_cast<int>(type))};
}

Query subtypeSourceQuery(BonusType type, BonusSubtypeID subtype, BonusSource source)
{
	return {Selector::typeSubtype(type, subtype).And(Selector::sourceTypeSel(source)),
		"type_" + std::to_string(static_cast<int>(type)) + "_" + std::to_string(subtype.getNum())
			+ "_source_" + std::to_string(static_cast<int>(source))};
}

const Query & rangedQuery(BonusType type, bool shooting)
{
	static const auto queries = []
	{
		std::array<Query, 6> result;
		const std::array types{BonusType::ENEMY_DEFENCE_REDUCTION,
			BonusType::ENEMY_ATTACK_REDUCTION, BonusType::GENERAL_ATTACK_REDUCTION};
		for(size_t i = 0; i < types.size(); ++i)
			for(size_t range = 0; range < 2; ++range)
			{
				auto [selector, key] = typeQuery(types[i]);
				const auto limited = range ? BonusLimitEffect::ONLY_DISTANCE_FIGHT : BonusLimitEffect::ONLY_MELEE_FIGHT;
				result[i * 2 + range] = {selector.And(Selector::effectRange()(BonusLimitEffect::NO_LIMIT)
					.Or(Selector::effectRange()(limited))), key + (range ? "_ranged" : "_melee")};
			}
		return result;
	}();
	const size_t index = type == BonusType::ENEMY_DEFENCE_REDUCTION ? 0
		: type == BonusType::ENEMY_ATTACK_REDUCTION ? 1 : 2;
	return queries[index * 2 + (shooting ? 1 : 0)];
}

double rangedValue(const battle::Unit & unit, const UnitFacts & facts, BonusType type, bool shooting)
{
	if(!facts.has(type)) return 0;
	const auto & [selector, key] = rangedQuery(type, shooting);
	return unit.valOfBonuses(selector, key);
}

void presence(const battle::Unit & unit, UnitFacts & facts)
{
	const auto bonuses = unit.getAllBonuses(Selector::all, "all");
	for(const auto & bonus : *bonuses)
	{
		const auto index = static_cast<size_t>(bonus->type);
		if(index < bonusTypeCount) facts.bonuses[index] = true;
	}
}

void value(const battle::Unit & unit, UnitFacts & facts, BonusType type)
{
	if(facts.has(type)) facts.values[static_cast<size_t>(type)] = unit.valOfBonuses(type);
}

std::string creatureKey(const battle::Unit & unit)
{
	return unit.creatureId().toEntity(LIBRARY)->getJsonKey();
}

void captureBase(const DamageAttackInfo & info, Facts & facts)
{
	const auto & unit = *info.attacker;
	auto & attacker = facts.attacker;
	value(unit, attacker, BonusType::ALWAYS_MAXIMUM_DAMAGE);
	value(unit, attacker, BonusType::ALWAYS_MINIMUM_DAMAGE);
	attacker.minDamage = unit.getMinDamage(info.shooting);
	attacker.maxDamage = unit.getMaxDamage(info.shooting);
	if(attacker.minDamage > attacker.maxDamage)
	{
		attacker.creatureKey = creatureKey(unit);
		std::ostringstream error;
		error << "creature " << attacker.creatureKey << " has minimal damage (" << attacker.minDamage
			<< ") greater than maximal damage (" << attacker.maxDamage << ")";
		throw std::runtime_error(error.str());
	}
	if(attacker.has(BonusType::SIEGE_WEAPON))
	{
		attacker.turret = unit.isTurret();
		if(!attacker.turret && info.machineBaseDamage <= 0 && info.siegeSkillMultiplier <= 0)
		{
			static const auto artifact = subtypeSourceQuery(BonusType::PRIMARY_SKILL, PrimarySkill::ATTACK, BonusSource::ARTIFACT);
			static const auto base = subtypeSourceQuery(BonusType::PRIMARY_SKILL, PrimarySkill::ATTACK, BonusSource::HERO_BASE_SKILL);
			facts.heroArtifactAttack = unit.valOfBonuses(artifact.first, artifact.second);
			facts.heroBaseAttack = unit.valOfBonuses(base.first, base.second);
		}
	}
	attacker.count = unit.getCount();
}

void captureAttackDefense(const DamageAttackInfo & info, Facts & facts)
{
	const auto & attacker = *info.attacker;
	const auto & defender = *info.defender;
	facts.attacker.attack = attacker.getAttack(info.shooting);
	value(attacker, facts.attacker, BonusType::IN_FRENZY);
	if(facts.attacker.value(BonusType::IN_FRENZY) != 0)
	{
		facts.attacker.defense = attacker.getDefense(info.shooting)
			+ (info.physicalDamage ? attacker.valOfBonuses(BonusType::PHYSICAL_DEFENSE) : 0);
		facts.defenderDefenseReduction = rangedValue(defender, facts.defender, BonusType::ENEMY_DEFENCE_REDUCTION, info.shooting);
	}
	if(facts.defender.has(BonusType::KING))
	{
		const auto slayer = attacker.getBonusesOfType(BonusType::SLAYER);
		if(!slayer->empty())
		{
			facts.hasSlayer = true;
			const auto & first = slayer->front();
			facts.slayerMastery = first->parameters ? first->parameters->toNumber() : 0;
			value(defender, facts.defender, BonusType::KING);
			facts.slayerValue = first->val;
		}
	}
	facts.defenderAttackReduction = rangedValue(defender, facts.defender, BonusType::ENEMY_ATTACK_REDUCTION, info.shooting);
	if(!facts.defender.has(BonusType::IN_FRENZY))
	{
		facts.defender.defense = defender.getDefense(info.shooting)
			+ (info.physicalDamage ? defender.valOfBonuses(BonusType::PHYSICAL_DEFENSE) : 0);
		facts.attackerDefenseReduction = rangedValue(attacker, facts.attacker, BonusType::ENEMY_DEFENCE_REDUCTION, info.shooting);
	}
}

void captureOrdinaryFactors(const CBattleInfoCallback & battle, const DamageAttackInfo & info, Facts & facts)
{
	const auto & attacker = *info.attacker;
	const auto & defender = *info.defender;
	if(facts.attacker.has(BonusType::PERCENTAGE_DAMAGE_BOOST))
		facts.damageBoost = attacker.valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST,
			info.shooting ? BonusCustomSubtype::damageTypeRanged : BonusCustomSubtype::damageTypeMelee);
	value(attacker, facts.attacker, BonusType::GENERAL_DAMAGE_PREMY);
	if(info.chargeDistance > 0 && !facts.defender.has(BonusType::CHARGE_IMMUNITY))
		value(attacker, facts.attacker, BonusType::JOUSTING);
	if(info.doubleDamage)
	{
		facts.attacker.creatureKey = creatureKey(attacker);
		if(facts.attacker.has(BonusType::BONUS_DAMAGE_PERCENTAGE))
			facts.doubleDamageBonus = attacker.valOfBonuses(BonusType::BONUS_DAMAGE_PERCENTAGE, attacker.creatureId());
	}
	if(facts.attacker.has(BonusType::HATE))
	{
		facts.defender.creatureKey = creatureKey(defender);
		facts.hatedCreature = attacker.valOfBonuses(BonusType::HATE, defender.creatureId());
	}
	if(info.shooting && !info.archeryAdjacentRangedTarget)
		facts.distancePenalty = battle.battleHasDistancePenalty(&attacker, info.attackerHex, info.defenderHex);
	if(!info.shooting && !facts.attacker.has(BonusType::NO_MELEE_PENALTY)) facts.attacker.shooter = attacker.isShooter();
	if(info.shooting && !info.archeryIgnoreObstaclePenalty && !info.archeryHighArc)
		facts.wallPenalty = battle.battleHasWallPenalty(&attacker, info.attackerHex, info.defenderHex);
	facts.attackerAttackReduction = rangedValue(attacker, facts.attacker, BonusType::GENERAL_ATTACK_REDUCTION, info.shooting);
	if(info.shooting) value(attacker, facts.attacker, BonusType::FORGETFULL);
	if(facts.defender.has(BonusType::GENERAL_DAMAGE_REDUCTION))
	{
		static const auto petrified = subtypeSourceQuery(BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusCustomSubtype::damageTypeAll, BonusSource::SPELL_EFFECT);
		facts.petrification = defender.valOfBonuses(petrified.first, petrified.second);
	}
}

void captureAdditionalFactors(const CBattleInfoCallback & battle, const DamageAttackInfo & info, Facts & facts)
{
	const auto & attacker = *info.attacker;
	const auto & defender = *info.defender;
	value(defender, facts.defender, BonusType::LEVEL_SPELL_IMMUNITY);
	if(facts.defender.value(BonusType::LEVEL_SPELL_IMMUNITY) >= 5 || facts.defender.has(BonusType::MIND_IMMUNITY))
		facts.attacker.creatureKey = creatureKey(attacker);
	value(defender, facts.defender, BonusType::VULNERABLE_FROM_BACK);
	if(facts.defender.value(BonusType::VULNERABLE_FROM_BACK) != 0)
		facts.toReverse = battle.isToReverse(&attacker, &defender, info.attackerHex, info.defenderHex);
	if(facts.attacker.has(BonusType::HATES_TRAIT))
	{
		const auto hates = attacker.getBonusesOfType(BonusType::HATES_TRAIT);
		BonusList matched;
		for(const auto & hate : *hates)
		{
			const auto type = static_cast<BonusType>(BonusTypeID::decode(hate->subtype.toString()));
			if(defender.hasBonusOfType(type)) matched.push_back(hate);
		}
		facts.hatedTraits = matched.totalValue();
	}
	if(facts.attacker.has(BonusType::REVENGE))
	{
		facts.attacker.maxHealth = attacker.getMaxHealth();
		facts.attacker.baseAmount = attacker.unitBaseAmount();
		facts.attacker.availableHealth = static_cast<double>(attacker.getAvailableHealth());
	}
}
}

Facts capture(const CBattleInfoCallback & battle, const DamageAttackInfo & info)
{
	Facts facts;
	presence(*info.attacker, facts.attacker);
	presence(*info.defender, facts.defender);
	captureBase(info, facts);
	if(info.luckyStrike && (info.newHorizonsFinalLuck || info.sylvanLuckyStrikeTotalPercent <= 0))
		value(*info.attacker, facts.attacker, BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE);
	facts.defender.phantomIntegrity = static_cast<double>(info.defender->getPhantomIntegrity());
	facts.physicalProtection = capturePhysicalProtection(info,
		facts.defender.has(BonusType::GENERAL_DAMAGE_REDUCTION), facts.defender.has(BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS));
	captureAttackDefense(info, facts);
	captureOrdinaryFactors(battle, info, facts);
	captureAdditionalFactors(battle, info, facts);
	value(*info.defender, facts.defender, BonusType::DAMAGE_RECEIVED_CAP);
	if(facts.defender.value(BonusType::DAMAGE_RECEIVED_CAP) > 0)
		facts.defender.maxHealth = info.defender->getMaxHealth();
	if(facts.defender.phantomIntegrity <= 0) facts.defender.firstHealth = info.defender->getFirstHPleft();
	if(facts.defender.phantomIntegrity <= 0) facts.defender.maxHealth = info.defender->getMaxHealth();
	facts.defender.count = info.defender->getCount();
	return facts;
}
}
