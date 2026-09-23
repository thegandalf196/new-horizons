/*
 * MagicalDamageReduction.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "MagicalDamageReduction.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <stdexcept>

namespace spells
{
namespace
{
using BigInteger = boost::multiprecision::cpp_int;

int64_t floorScaledDamage(int64_t rawDamage, const BigInteger & remainingNumerator,
	const BigInteger & remainingDenominator)
{
	const BigInteger result = BigInteger(rawDamage) * remainingNumerator / remainingDenominator;
	return result.convert_to<int64_t>();
}
}

MagicalDamageReductionResult calculateMagicalDamageReduction(
	int64_t rawDamage, const std::vector<int> & independentReductionsPercent, int penetrationPercent)
{
	// Validate the complete request before any zero-damage or capped-reduction
	// shortcut could hide an invalid trailing value.
	if(rawDamage < 0 || penetrationPercent < 0 || penetrationPercent > 100)
		throw std::invalid_argument("Invalid magical damage reduction inputs");
	for(const int reduction : independentReductionsPercent)
		if(reduction < 0 || reduction > 100)
			throw std::invalid_argument("Magical damage reduction percentages must be between 0 and 100");
	if(rawDamage == 0)
		return {};

	BigInteger remainingNumerator = 1;
	BigInteger remainingDenominator = 1;
	for(const int reduction : independentReductionsPercent)
	{
		if(reduction == 0)
			continue;
		// Each independent source contributes its own remaining fraction:
		// remaining = Π(100 - reduction_i) / 100^n.
		remainingNumerator *= 100 - reduction;
		remainingDenominator *= 100;
		if(remainingNumerator * 20 <= remainingDenominator)
		{
			// All inputs were already validated. Remaining factors cannot increase
			// the product, so the 95% cap is now fixed at exactly 1/20 remaining.
			remainingNumerator = 1;
			remainingDenominator = 20;
			break;
		}
	}

	const auto damageWithoutPenetration = floorScaledDamage(
		rawDamage, remainingNumerator, remainingDenominator);

	// Penetration p reduces the aggregate MDR by the relative factor (100-p)/100:
	// damage fraction = 1 - (1 - remaining) * (100-p)/100.
	const BigInteger penetratedDenominator = remainingDenominator * 100;
	const BigInteger penetratedNumerator = remainingNumerator * 100
		+ (remainingDenominator - remainingNumerator) * penetrationPercent;
	const auto damageWithPenetration = floorScaledDamage(
		rawDamage, penetratedNumerator, penetratedDenominator);

	return {damageWithoutPenetration, damageWithPenetration};
}
}
