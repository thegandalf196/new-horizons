/*
 * PercentageFormatting.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
#include <string>

namespace TextOperations
{
inline std::string formatBasisPoints(int64_t basisPoints)
{
	const bool negative = basisPoints < 0;
	const auto absolute = negative ? -basisPoints : basisPoints;
	const auto wholePercent = absolute / 100;
	const auto fractionalPercent = absolute % 100;

	std::string result = (negative ? "-" : "") + std::to_string(wholePercent);
	if(fractionalPercent != 0)
	{
		std::string fraction = std::to_string(fractionalPercent + 100).substr(1);
		if(fraction.back() == '0')
			fraction.pop_back();
		result += "." + fraction;
	}
	return result + "%";
}
}
