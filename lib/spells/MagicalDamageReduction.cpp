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

#include <boost/multiprecision/cpp_int.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace spells
{
std::vector<int> capturedMdrPenetrations(const JsonNode & captured, uint32_t targetUnitId)
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
}
