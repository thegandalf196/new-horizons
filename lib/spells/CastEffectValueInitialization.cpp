/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPlacementPower.h"
#include "NewHorizonsMagicArrow.h"
#include "NewHorizonsDirectDamage.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsRestorationPools.h"
#include "NewHorizonsCureHealing.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsTimedMagnitudes.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
namespace spells
{
void BaseMechanics::initializeEffectValue(const IBattleCast * event)
{
	const auto value = event->getEffectValue();
	effectValueWasOverridden = value.has_value();
	const auto * reanimateBattle = cb->getBattle();
	const auto * reanimateHero = caster->getHeroCaster();
	if(reanimateBattle && reanimateHero && newHorizonsMagic::reanimateEnabled(
		reanimateBattle->getMagicRules(), owner->getId()))
	{
		// Re-animate historically derives its pool from raw hero Attributes,
		// ignoring generic effect-value overrides. Keep that contract while
		// sharing the same rational pool with its preview and specialty.
		effectValue = *newHorizonsMagic::reanimateBaseHealingPool(
			reanimateBattle->getMagicRules(), reanimateHero, owner->getId(),
			std::max(reanimateHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 0),
			getCastSpellPowerComponentBonusPercent(), getSpellPowerCoefficientBasisPoints());
	}
	else if(value.has_value())
		effectValue = *value; // Explicit zero is an override, not absence.
	else if(const auto casterValue = caster->getEffectValue(owner); casterValue != 0)
		effectValue = casterValue; // Legacy numeric caster zero means absence.
	else
	{
	const auto * battle = cb->getBattle();
	const int spellPowerCoefficientBasisPoints = getSpellPowerCoefficientBasisPoints();
	const auto * heroCaster = caster->getHeroCaster();
	const bool damageSpell = owner->isDamage() || usesNewHorizonsEarthquake();
	const int damagePerkBonusPercent = battle && damageSpell
		? newHorizonsMagic::spellPowerDamagePerkBonusPercent(battle->getMagicRules(), heroCaster, owner)
		: 0;
	const int damageSpecialtyPercent = damageSpell && heroCaster
		? heroCaster->getDamageSpellSpecialtyBonusPercent(owner->getId())
		: 0;
	const int damageCoefficientBasisPoints = damageSpell
		? getDirectDamageSpellPowerCoefficientBasisPoints() * (100 + damagePerkBonusPercent) / 100
		: 10000;
	const int effectPowerCoefficientBasisPoints = damageSpell
		? damageCoefficientBasisPoints
		: spellPowerCoefficientBasisPoints;
		const int empowerBonusPercent = battle
			? newHorizonsMagic::empowerSpellBonusPercent(
				battle->getMagicRules(), caster->getHeroCaster(), owner->getId(), isMassSlow() ? 3 : 1)
			: 0;
		if(usesNewHorizonsEarthquake())
		{
			effectValue = newHorizonsMagic::earthquakeRawDamage(
				battle->getMagicRules()["spells"]["core:earthquake"]["earthquake"], effectPower,
				damageCoefficientBasisPoints, getEmpowerSpellBonusPercent());
		}
		else if(battle && owner->getJsonKey() == "new-horizons:hydrasVitality"
			&& newHorizonsMagic::rulesActive(battle->getMagicRules())
			&& battle->getMagicRules()["rulesetVersion"].Integer()
				>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		{
			// Millionths of one percent preserve fractional School scaling
			// until the effect rounds the resulting creature HP. Capacity uses
			// the canonical raw attribute, not the legacy divisor.
			const auto * hero = caster->getHeroCaster();
			const int specialtyPercent = hero ? hero->getNonDamageSpellSpecialtyBonusPercent(owner->getId()) : 0;
			effectValue = newHorizonsMagic::hydraCapacityMillionths(effectPower, spellPowerCoefficientBasisPoints, getEmpowerSpellBonusPercent(), specialtyPercent);
		}
		else if(battle && newHorizonsMagic::cureEnabled(battle->getMagicRules(), owner->getId()))
		{
			effectValue = newHorizonsMagic::cureHealingBudget(caster->getHeroCaster(), effectPower,
				spellPowerCoefficientBasisPoints, getEmpowerSpellBonusPercent());
		}
		else if(battle && newHorizonsMagic::resurrectionRestorationEnabled(
			battle->getMagicRules(), owner->getId()))
		{
			// Both the fixed pool and the Spell Power-derived pool remain separate:
			// School/Spellcraft, shared cast modifiers and the specialty affect only
			// the raw Spell Power component.
			const auto * hero = caster->getHeroCaster();
			const int specialtyPercent = hero
				? hero->getNonDamageSpellSpecialtyBonusPercent(owner->getId()) : 0;
			effectValue = newHorizonsMagic::resurrectionRestorationPool(effectPower,
				spellPowerCoefficientBasisPoints, getEmpowerSpellBonusPercent(), specialtyPercent);
		}
		else
		{
			const auto modifiers = newHorizonsMagic::magicArrowOverchargeModifiers(
				dynamic_cast<const CGHeroInstance *>(caster));
			std::optional<int64_t> magicArrowValue;
			if(battle && newHorizonsMagic::magicArrowOverchargeEnabled(battle->getMagicRules(), owner->getId()))
			{
				const int capPower = newHorizonsMagic::requiresMagicArrowChargeRules(battle->getMagicRules()) && heroCaster
					? heroCaster->getPrimSkillLevel(PrimarySkill::SPELL_POWER) : effectPower;
				magicArrowValue = newHorizonsMagic::magicArrowDamageBasisPoints(battle->getMagicRules(), owner->getId(),
					effectPower, getEffectPowerDivisor(), getOvercharge(), modifiers, damageCoefficientBasisPoints,
					empowerBonusPercent, damageSpecialtyPercent, capPower);
			}

			std::optional<int64_t> savedValue;
			if(battle && !magicArrowValue)
			{
				if(const auto formula = newHorizonsMagic::spellDirectDamage(
					battle->getMagicRules(), owner->getJsonKey()))
					savedValue = formula->evaluateBasisPoints(
						effectPower, getEffectPowerDivisor(), damageCoefficientBasisPoints, empowerBonusPercent,
						damageSpecialtyPercent);
			}

			if(magicArrowValue)
				effectValue = *magicArrowValue;
			else if(savedValue)
				effectValue = *savedValue;
			else if(effectPowerCoefficientBasisPoints != 10000
				|| empowerBonusPercent > 0 || damageSpecialtyPercent > 0)
			{
				const int64_t powerNumerator = static_cast<int64_t>(owner->getBasePower()) * effectPower;
				effectValue = owner->getLevelPower(effectLevel)
					+ scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
						powerNumerator, getEffectPowerDivisor(), effectPowerCoefficientBasisPoints, damageSpecialtyPercent);
			}
			else
				effectValue = owner->calculateRawEffectValue(effectLevel, effectPower, 1, getEffectPowerDivisor());
		}
	}
	vstd::amax(effectValue, 0);
}
}
