/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
namespace battle
{
std::pair<int32_t, int32_t> CUnitState::getMoraleLimits() const
{
	if(env)
	{
		const auto savedLimits = env->unitMoraleLimits(this);
		if(savedLimits)
			return *savedLimits;
	}
	return AFactionMember::getMoraleLimits();
}
std::pair<int32_t, int32_t> CUnitState::getLuckLimits() const
{
	if(env)
	{
		const auto savedLimits = env->unitLuckLimits(this);
		if(savedLimits)
			return *savedLimits;
	}
	return AFactionMember::getLuckLimits();
}
}
