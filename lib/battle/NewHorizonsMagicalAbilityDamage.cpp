/*
 * NewHorizonsMagicalAbilityDamage.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMagicalAbilityDamage.h"
#include "NewHorizonsNecromancyCombat.h"

#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../spells/MagicalDamageReduction.h"
#include "../spells/IncomingMagicalDamage.h"
#include <limits>
#include <stdexcept>

namespace newHorizonsMagicalAbilityDamage
{
int64_t adjustDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawDamage)
{
	if(rawDamage <= 0)
		return 0;

	if(!battle.battleUsesNewHorizonsMultiplicativeMDR())
		return spells::adjustUnmitigatedMagicalDamage(target, rawDamage);

	std::vector<int> reductionSourcesBasisPoints;
	const int intrinsicReduction = target.intrinsicMagicalDamageReductionPercent();
	if(intrinsicReduction > 0)
		reductionSourcesBasisPoints.push_back(std::clamp(intrinsicReduction, 0, 100) * 100);
	const auto anySchoolReductions = target.getBonuses(
		Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(SpellSchool::ANY)),
		"new_horizons_ability_damage_any_school_reduction");
	for(const auto & bonus : *anySchoolReductions)
	{
		const int reduction = std::clamp(bonus->val, 0, 100);
		if(reduction > 0)
			reductionSourcesBasisPoints.push_back(reduction * 100);
	}

	const auto anySchoolReductionsBasisPoints = target.getBonuses(
		Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
			BonusSubtypeID(SpellSchool::ANY)),
		"new_horizons_ability_damage_any_school_reduction_basis_points");
	for(const auto & bonus : *anySchoolReductionsBasisPoints)
	{
		const int reduction = std::clamp(bonus->val, 0, 10'000);
		if(reduction > 0)
			reductionSourcesBasisPoints.push_back(reduction);
	}

	const int holdTheLineReduction = std::clamp(
		battle.battleGetHoldTheLineMagicalReductionBasisPoints(&target), 0, 10'000);
	if(holdTheLineReduction > 0)
		reductionSourcesBasisPoints.push_back(holdTheLineReduction);

	const int perkReduction = std::clamp(
		battle.battleGetPerkMagicalReductionBasisPoints(&target), 0, 10'000);
	if(perkReduction > 0)
		reductionSourcesBasisPoints.push_back(perkReduction);

	const auto mitigated = spells::calculateMagicalDamageReductionBasisPoints(
		rawDamage, reductionSourcesBasisPoints, spells::targetMdrPenetrations(target)).damageWithPenetration;
	return spells::applyIncomingMagicalDamageModifier(target, mitigated);
}
bool fireImmune(const battle::Unit & target)
{
	const BonusSubtypeID fire(SpellSchool::FIRE);
	if(target.hasBonusOfType(BonusType::SPELL_SCHOOL_IMMUNITY, fire)
		|| target.hasBonusOfType(BonusType::NEGATIVE_EFFECTS_IMMUNITY, fire)
		|| target.hasBonusOfType(BonusType::NEGATIVE_EFFECTS_IMMUNITY, BonusSubtypeID(SpellSchool::ANY)))
		return true;
	const auto percentReductions = target.getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION, fire);
	for(const auto & bonus : *percentReductions)
		if(bonus->val >= 100)
			return true;
	const auto fractionalReductions = target.getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, fire);
	for(const auto & bonus : *fractionalReductions)
		if(bonus->val >= 10'000)
			return true;
	return false;
}
int64_t adjustFireDamage(const FireDamageContext & context, const battle::Unit & target, int64_t rawDamage)
{
	if(rawDamage <= 0)
		return 0;
	if(!context.usesMultiplicativeMDR())
		throw std::runtime_error("Creature burn requires captured multiplicative magical damage reduction");
	if(target.isInvincible() || fireImmune(target))
		return 0;
	std::vector<int> reductions;
	for(const auto school : {SpellSchool::FIRE, SpellSchool::ANY})
	{
		const auto percentReductions = target.getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(school));
		for(const auto & bonus : *percentReductions)
			if(bonus->val > 0)
				reductions.push_back(std::clamp(bonus->val, 0, 100) * 100);
		const auto fractionalReductions = target.getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSubtypeID(school));
		for(const auto & bonus : *fractionalReductions)
			if(bonus->val > 0)
				reductions.push_back(std::clamp(bonus->val, 0, 10'000));
	}
	for(const int value : {target.intrinsicMagicalDamageReductionPercent() * 100,
		context.holdTheLineReductionBasisPoints(target),
		context.perkReductionBasisPoints(target)})
		if(value > 0)
			reductions.push_back(std::clamp(value, 0, 10'000));
	int64_t result = spells::calculateMagicalDamageReductionBasisPoints(rawDamage, reductions,
		spells::targetMdrPenetrations(target)).damageWithPenetration;
	if(target.getPhantomIntegrity() > 0)
		result = result > std::numeric_limits<int64_t>::max() / 2
			? std::numeric_limits<int64_t>::max() : result * 2;
	result = spells::applyIncomingElementalDamageModifier(target, SpellDamageElement::FIRE, result);
	result = spells::applyIncomingMagicalDamageModifier(target, result);
	const int cap = target.valOfBonuses(BonusType::DAMAGE_RECEIVED_CAP);
	return cap > 0 ? std::min<int64_t>(result, static_cast<int64_t>(target.getMaxHealth()) * cap / 100) : result;
}
int64_t adjustFireDamage(const CBattleInfoCallback & battle, const battle::Unit & target, int64_t rawDamage)
{
	class CallbackContext final : public FireDamageContext
	{
		const CBattleInfoCallback & callback;
	public:
		explicit CallbackContext(const CBattleInfoCallback & source) : callback(source) {}
		bool usesMultiplicativeMDR() const override
		{
			return callback.battleUsesNewHorizonsMultiplicativeMDR();
		}
		int holdTheLineReductionBasisPoints(const battle::Unit & unit) const override
		{
			return callback.battleGetHoldTheLineMagicalReductionBasisPoints(&unit);
		}
		int perkReductionBasisPoints(const battle::Unit & unit) const override
		{
			return callback.battleGetPerkMagicalReductionBasisPoints(&unit);
		}
	};
	const CallbackContext context(battle);
	return adjustFireDamage(context, target, rawDamage);
}
}
