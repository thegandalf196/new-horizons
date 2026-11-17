/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CHealth.h"
#include "CUnitState.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace battle
{
int64_t CHealth::maximumDamageForCasualties(int32_t casualties) const
{
	if(casualties < 0) throw std::invalid_argument("Negative permitted casualty count");
	if(getCount() == 0) return 0;
	if(casualties >= getCount()) return std::numeric_limits<int64_t>::max();
	// Native damage consumes the leading creature, then ordered wounded cohorts,
	// then full-health rear creatures. Stop one HP before the next forbidden death.
	int64_t threshold = firstHPleft;
	int32_t remaining = casualties;
	if(capacityHealthTracking)
	{
		for(const auto & cohort : capacityHealthCohorts)
		{
			const auto used = std::min(remaining, cohort.count);
			threshold += static_cast<int64_t>(used) * cohort.hitPoints;
			remaining -= used;
			if(remaining == 0) break;
		}
	}
	if(remaining > 0)
		threshold += static_cast<int64_t>(remaining) * (capacityHealthTracking ? capacityHealthMax : maximumPerCreature());
	return std::max<int64_t>(0, threshold - 1);
}
std::optional<int64_t> CUnitState::maximumBodyDamageForCasualties(int32_t casualties) const
{
	return health.maximumDamageForCasualties(casualties);
}
}
