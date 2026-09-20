/*
 * NewHorizonsMovement.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMovement.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace newHorizonsMovement
{
namespace
{
int ceilRational(const int64_t numerator, const int64_t denominator)
{
	if(numerator < 0 || denominator <= 0)
		throw std::invalid_argument("Invalid New Horizons movement rational");
	const auto value = (numerator + denominator - 1) / denominator;
	return static_cast<int>(std::min<int64_t>(value, std::numeric_limits<int>::max()));
}

int64_t saturatingAdd(const int64_t lhs, const int64_t rhs)
{
	if(rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs)
		return std::numeric_limits<int64_t>::max();
	if(rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs)
		return std::numeric_limits<int64_t>::min();
	return lhs + rhs;
}

int64_t saturatingMultiply(const int64_t lhs, const int64_t rhs)
{
	if(lhs == 0 || rhs == 0)
		return 0;
	if(lhs == -1 && rhs == std::numeric_limits<int64_t>::min())
		return std::numeric_limits<int64_t>::max();
	if(rhs == -1 && lhs == std::numeric_limits<int64_t>::min())
		return std::numeric_limits<int64_t>::max();

	if(lhs > 0)
	{
		if(rhs > 0 && lhs > std::numeric_limits<int64_t>::max() / rhs)
			return std::numeric_limits<int64_t>::max();
		if(rhs < 0 && rhs < std::numeric_limits<int64_t>::min() / lhs)
			return std::numeric_limits<int64_t>::min();
	}
	else
	{
		if(rhs > 0 && lhs < std::numeric_limits<int64_t>::min() / rhs)
			return std::numeric_limits<int64_t>::min();
		if(rhs < 0 && lhs < std::numeric_limits<int64_t>::max() / rhs)
			return std::numeric_limits<int64_t>::max();
	}
	return lhs * rhs;
}

int64_t applyPercentageRoundDown(const int64_t base, const int64_t percentage)
{
	const int64_t factor = saturatingAdd(100, percentage);
	return saturatingMultiply(base, factor) / 100;
}
}

int maximumDailyMovement(const int64_t percentage, const int64_t flat)
{
	return maximumDailyMovement(BASE_DAILY_MOVEMENT, percentage, 0, flat);
}

int maximumDailyMovement(const int64_t baseValue, const int64_t percentageToBase,
	const int64_t percentageToAll, const int64_t additive)
{
	// BonusList values are authored as ints, but source/target modifiers and
	// several stacked effects are accumulated before this helper is called.
	// Keep that accumulation wide and saturate only when the final public int
	// movement pool is produced.  Percentages below -100 naturally floor a
	// positive base at zero; there is no arbitrary authoring-range rejection.
	int64_t total = applyPercentageRoundDown(baseValue, percentageToBase);
	total = saturatingAdd(total, additive);
	total = applyPercentageRoundDown(total, percentageToAll);
	return static_cast<int>(std::clamp<int64_t>(total, 0, std::numeric_limits<int>::max()));
}

int stepCost(const bool diagonal, const bool terrainAffinity, const bool desert, const bool road)
{
	const int base = diagonal ? DIAGONAL_STEP_COST : ORTHOGONAL_STEP_COST;

	// Non-native terrain is 7/5, desert/sand is 9/5, and roads reduce the
	// already terrain-adjusted result by 33% (67/100).  A hero/army with
	// terrain affinity is ordinary terrain even when the tile is sand; the
	// default faction data contains no sand-native faction, while scenarios can
	// explicitly provide one through the normal native-terrain data.
	const int terrainNumerator = desert && !terrainAffinity ? 9 : (terrainAffinity ? 1 : 7);
	const int terrainDenominator = desert && !terrainAffinity ? 5 : (terrainAffinity ? 1 : 5);
	const int roadNumerator = road ? 67 : 1;
	const int roadDenominator = road ? 100 : 1;

	return ceilRational(static_cast<int64_t>(base) * terrainNumerator * roadNumerator,
		static_cast<int64_t>(terrainDenominator) * roadDenominator);
}
}
