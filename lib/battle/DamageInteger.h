/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
#include <limits>

namespace damageInteger
{
inline int64_t boundedNonnegative(double value)
{
	// INT64_MAX rounds upward to 2^63 as a double; compare before casting.
	if(value >= static_cast<double>(std::numeric_limits<int64_t>::max()))
		return std::numeric_limits<int64_t>::max();
	return value > 0 ? static_cast<int64_t>(value) : 0;
}
}
