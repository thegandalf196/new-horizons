/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ResidualLife.h"
#include <algorithm>
#include <stdexcept>
namespace battle::residualLife
{
Transition record(int64_t previous, int64_t damage, int32_t threshold, int32_t limit)
{
	if(previous < 0 || damage < 0 || threshold <= 0 || limit <= 0)
		throw std::invalid_argument("Invalid Residual Life transition");
	const int64_t ceiling = static_cast<int64_t>(threshold) * limit;
	if(previous > ceiling)
		throw std::invalid_argument("Residual Life progress exceeds its combat cap");
	const auto next = previous + std::min(damage, ceiling - previous);
	return {next, static_cast<int32_t>(next / threshold - previous / threshold)};
}
}
