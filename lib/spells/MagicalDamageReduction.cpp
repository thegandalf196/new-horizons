/*
 * MagicalDamageReduction.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "MagicalDamageReduction.h"
#include "../json/JsonNode.h"
#include "../battle/IBattleState.h"
#include "CSpell.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/Bonus.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace spells
{
std::optional<CapturedOverwhelmingFormula> capturedOverwhelmingFormula(const JsonNode & captured)
{
	if(!captured.isStruct())
		return std::nullopt;
	const auto & side = captured["overwhelmingFormulaSide"];
	const auto & token = captured["overwhelmingFormulaToken"];
	if(!side.isNumber() || !std::isfinite(side.Float())
		|| (side.Float() != static_cast<int>(BattleSide::ATTACKER)
			&& side.Float() != static_cast<int>(BattleSide::DEFENDER))
		|| !token.isString())
		return std::nullopt;
	const auto & decimal = token.String();
	if(decimal.empty() || decimal.size() > 20 || decimal.front() == '0')
		return std::nullopt;
	uint64_t value = 0;
	for(const char character : decimal)
	{
		if(character < '0' || character > '9')
			return std::nullopt;
		const auto digit = static_cast<uint64_t>(character - '0');
		if(value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
			return std::nullopt;
		value = value * 10 + digit;
	}
	return CapturedOverwhelmingFormula{static_cast<BattleSide>(side.Integer()), value};
}

std::vector<int> capturedMdrPenetrations(const JsonNode & captured, uint32_t targetUnitId,
	const IBattleInfo * battle)
{
	if(!captured.isStruct())
		return {};
	std::vector<int> result;
	// Lua serializes numbers as floats. Admit only exact integral values in the
	// same bounded domain as native integer payloads, without truncating input.
	const auto validInteger = [](const JsonNode & value, double maximum)
	{
		return value.isNumber() && std::isfinite(value.Float())
			&& value.Float() >= 0 && value.Float() <= maximum
			&& std::floor(value.Float()) == value.Float();
	};
	const auto & contributors = captured["penetrations"];
	if(!contributors.isNull())
	{
		if(!contributors.isVector() || contributors.Vector().size() > 32)
			return {};
		for(const auto & contributor : contributors.Vector())
		{
			if(!validInteger(contributor, 100))
				return {};
			result.push_back(static_cast<int>(contributor.Integer()));
		}
	}
	const auto & focusedPercent = captured["focusedPenetrationPercent"];
	const auto & focusedTarget = captured["focusedTargetUnitId"];
	if(!focusedPercent.isNull() || !focusedTarget.isNull())
	{
		if(!validInteger(focusedPercent, 100)
			|| !validInteger(focusedTarget, std::numeric_limits<uint32_t>::max()))
			return {};
		if(static_cast<uint32_t>(focusedTarget.Integer()) == targetUnitId)
			result.push_back(static_cast<int>(focusedPercent.Integer()));
	}
	if(battle)
	{
		const auto formula = capturedOverwhelmingFormula(captured);
		if(formula && battle->getOverwhelmingFormulaState(formula->side).canPenetrate(formula->token))
			result.push_back(50);
	}
	return result;
}

namespace
{
using BigInteger = boost::multiprecision::cpp_int;

int64_t floorScaledDamage(int64_t rawDamage, const BigInteger & remainingNumerator,
	const BigInteger & remainingDenominator)
{
	const BigInteger result = BigInteger(rawDamage) * remainingNumerator / remainingDenominator;
	return result.convert_to<int64_t>();
}

MagicalDamageReductionResult calculateMagicalDamageReductionWithScale(
	int64_t rawDamage, const std::vector<int> & independentReductions, int scale,
	const std::vector<int> & independentPenetrationsPercent)
{
	// Validate the complete request before any zero-damage or capped-reduction
	// shortcut could hide an invalid trailing value.
	if(rawDamage < 0)
		throw std::invalid_argument("Invalid magical damage reduction inputs");
	for(const int penetration : independentPenetrationsPercent)
		if(penetration < 0 || penetration > 100)
			throw std::invalid_argument("Magical damage penetration is outside its supported range");
	for(const int reduction : independentReductions)
		if(reduction < 0 || reduction > scale)
			throw std::invalid_argument("Magical damage reduction is outside its supported range");
	if(rawDamage == 0)
		return {};

	BigInteger remainingNumerator = 1;
	BigInteger remainingDenominator = 1;
	for(const int reduction : independentReductions)
	{
		if(reduction == 0)
			continue;
		// Each independent source contributes its own remaining fraction.
		remainingNumerator *= scale - reduction;
		remainingDenominator *= scale;
		if(remainingNumerator * 20 <= remainingDenominator)
		{
			// Remaining factors cannot increase the product, so the 95% cap is
			// now fixed at exactly 1/20 remaining.
			remainingNumerator = 1;
			remainingDenominator = 20;
			break;
		}
	}

	const auto damageWithoutPenetration = floorScaledDamage(
		rawDamage, remainingNumerator, remainingDenominator);

	BigInteger penetrationRemainingNumerator = 1;
	BigInteger penetrationRemainingDenominator = 1;
	for(const int penetration : independentPenetrationsPercent)
	{
		if(penetration == 0)
			continue;
		penetrationRemainingNumerator *= 100 - penetration;
		penetrationRemainingDenominator *= 100;
	}
	// Relative penetration scales the capped aggregate MDR by the product of
	// remaining fractions. Never round the combined penetration to whole percent.
	const BigInteger penetratedDenominator = remainingDenominator * penetrationRemainingDenominator;
	const BigInteger penetratedNumerator = penetratedDenominator
		- (remainingDenominator - remainingNumerator) * penetrationRemainingNumerator;
	const auto damageWithPenetration = floorScaledDamage(
		rawDamage, penetratedNumerator, penetratedDenominator);

	return {damageWithoutPenetration, damageWithPenetration};
}
}

MagicalDamageReductionResult calculateMagicalDamageReduction(
	int64_t rawDamage, const std::vector<int> & independentReductionsPercent, int penetrationPercent)
{
	return calculateMagicalDamageReductionWithScale(
		rawDamage, independentReductionsPercent, 100, {penetrationPercent});
}

MagicalDamageReductionResult calculateMagicalDamageReductionBasisPoints(
	int64_t rawDamage, const std::vector<int> & independentReductionsBasisPoints, int penetrationPercent)
{
	return calculateMagicalDamageReductionWithScale(
		rawDamage, independentReductionsBasisPoints, 10'000, {penetrationPercent});
}

MagicalDamageReductionResult calculateMagicalDamageReduction(
	int64_t rawDamage, const std::vector<int> & independentReductionsPercent,
	const std::vector<int> & independentPenetrationsPercent)
{
	return calculateMagicalDamageReductionWithScale(
		rawDamage, independentReductionsPercent, 100, independentPenetrationsPercent);
}

MagicalDamageReductionResult calculateMagicalDamageReductionBasisPoints(
	int64_t rawDamage, const std::vector<int> & independentReductionsBasisPoints,
	const std::vector<int> & independentPenetrationsPercent)
{
	return calculateMagicalDamageReductionWithScale(
		rawDamage, independentReductionsBasisPoints, 10'000, independentPenetrationsPercent);
}
std::vector<int> collectMagicalDamageReductionSources(const CSpell & spell, const battle::Unit * affectedCreature,
	int magicalDamageReductionBasisPoints, int perkMagicalDamageReductionBasisPoints,
	bool useFractionalMagicalDamageReduction)
{
	std::vector<int> sources;
	if(!affectedCreature || !spell.isMagical())
		return sources;
	const auto * bearer = affectedCreature->getBonusBearer();
	// Preserve the existing multi-school rule: the first matching school
	// supplies school-specific sources; each bonus remains independent.
	spell.forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		const BonusSubtypeID subtype(school);
		const auto reductions = bearer->getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION, subtype);
		const auto fractional = bearer->getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, subtype);
		if(!reductions->empty() || (useFractionalMagicalDamageReduction && !fractional->empty()))
		{
			for(const auto & bonus : *reductions)
			{
				const int reduction = std::clamp(bonus->val, 0, 100);
				if(reduction > 0)
					sources.push_back(reduction * 100);
			}
			if(useFractionalMagicalDamageReduction)
			{
				for(const auto & bonus : *fractional)
				{
					const int reduction = std::clamp(bonus->val, 0, 10000);
					if(reduction > 0)
						sources.push_back(reduction);
				}
			}
			stop = true;
		}
	});
	const auto anySchool = bearer->getBonuses(
		Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(SpellSchool::ANY)),
		"type_SPELL_DAMAGE_REDUCTION_s_ANY");
	for(const auto & bonus : *anySchool)
	{
		const int reduction = std::clamp(bonus->val, 0, 100);
		if(reduction > 0)
			sources.push_back(reduction * 100);
	}
	if(useFractionalMagicalDamageReduction)
	{
		const auto anySchoolFractional = bearer->getBonuses(
			Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSubtypeID(SpellSchool::ANY)),
			"type_SPELL_DAMAGE_REDUCTION_BASIS_POINTS_s_ANY");
		for(const auto & bonus : *anySchoolFractional)
		{
			const int reduction = std::clamp(bonus->val, 0, 10000);
			if(reduction > 0)
				sources.push_back(reduction);
		}
	}
	if(magicalDamageReductionBasisPoints > 0)
		sources.push_back(std::clamp(magicalDamageReductionBasisPoints, 0, 10000));
	if(perkMagicalDamageReductionBasisPoints > 0)
		sources.push_back(std::clamp(perkMagicalDamageReductionBasisPoints, 0, 10000));
	const int intrinsicReduction = affectedCreature->intrinsicMagicalDamageReductionPercent();
	if(intrinsicReduction > 0)
		sources.push_back(std::clamp(intrinsicReduction, 0, 100) * 100);
	return sources;
}
std::vector<int> targetMdrPenetrations(const battle::Unit & target)
{
	std::vector<int> result;
	const auto penetrationBonuses = target.getBonusesOfType(BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION);
	for(const auto & bonus : *penetrationBonuses)
		if(bonus->val > 0)
			result.push_back(std::clamp(bonus->val, 0, 100));
	return result;
}
int64_t applyTargetMdrPenetration(const battle::Unit & target, int64_t raw, int64_t mitigated)
{
	const auto penetrations = targetMdrPenetrations(target);
	if(penetrations.empty() || mitigated >= raw || raw <= 0)
		return mitigated;
	boost::multiprecision::cpp_int remainingNumerator = 1;
	boost::multiprecision::cpp_int remainingDenominator = 1;
	for(const int penetration : penetrations)
	{
		remainingNumerator *= 100 - penetration;
		remainingDenominator *= 100;
	}
	// Legacy staged floors remain intact. Only a new target modifier restores
	// its relative share of the actual positive reduction, never vulnerability.
	const boost::multiprecision::cpp_int numerator = boost::multiprecision::cpp_int(raw) * remainingDenominator
		- (boost::multiprecision::cpp_int(raw) - mitigated) * remainingNumerator;
	const boost::multiprecision::cpp_int result = numerator / remainingDenominator;
	return result.convert_to<int64_t>();
}
int64_t applyLegacyMagicalDamageReduction(const CSpell & spell, const battle::Unit * affectedCreature,
	const IBonusBearer * bearer, int64_t ret, int ignoreSpellDamageReductionPercent, int magicalDamageReductionBasisPoints)
{
	const int64_t raw = ret;

	// Legacy school-specific reduction semantics: when a spell has multiple
	// schools, only the first matching school's aggregate is applied.
	spell.forEachSchool([&](const SpellSchool & cnf, bool & stop)
	{
		if(bearer->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(cnf)))
		{
			const int reduction = bearer->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(cnf));
			const int effectiveReduction = reduction * (100 - ignoreSpellDamageReductionPercent) / 100;
			ret *= 100 - effectiveReduction;
			ret /= 100;
			stop = true; //only bonus from one school is used
		}
	});

	CSelector selector = Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(SpellSchool::ANY));
	auto cachingStr = "type_SPELL_DAMAGE_REDUCTION_s_ANY";

	//general spell dmg reduction, works only on magical effects
	if(bearer->hasBonus(selector, cachingStr) && spell.isMagical())
	{
		const int reduction = bearer->valOfBonuses(selector, cachingStr);
		const int effectiveReduction = reduction * (100 - ignoreSpellDamageReductionPercent) / 100;
		ret *= 100 - effectiveReduction;
		ret /= 100;
	}

	// Hold the Line's saved Iron Discipline value is an independent magical
	// reduction. Basis points preserve the exact half of an odd physical value.
	if(spell.isMagical() && magicalDamageReductionBasisPoints > 0)
	{
		const int boundedReductionBasisPoints = std::clamp(magicalDamageReductionBasisPoints, 0, 10000);
		const int effectiveReductionBasisPoints = boundedReductionBasisPoints
			* (100 - ignoreSpellDamageReductionPercent) / 100;
		const int remainingDamageBasisPoints = 10000
			- std::clamp(effectiveReductionBasisPoints, 0, 10000);
		ret = ret / 10000 * remainingDamageBasisPoints
			+ ret % 10000 * remainingDamageBasisPoints / 10000;
	}
	// A new captured unit passive may coexist with historical magic rules.
	// Retain every legacy reduction/floor above, then apply only this new source.
	if(spell.isMagical())
	{
		const int intrinsicReduction = affectedCreature->intrinsicMagicalDamageReductionPercent();
		if(intrinsicReduction > 0)
			ret = spells::calculateMagicalDamageReduction(ret, std::vector<int>{intrinsicReduction},
				ignoreSpellDamageReductionPercent).damageWithPenetration;
	}
	return spell.isMagical() ? applyTargetMdrPenetration(*affectedCreature, raw, ret) : ret;
}

}
