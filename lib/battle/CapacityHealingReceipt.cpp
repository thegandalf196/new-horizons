/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include <algorithm>
#include <stdexcept>
namespace battle
{
int64_t CHealth::consumeCapacityRegeneration(const int32_t perCreatureHeal, const int64_t receivedHealth)
{
	const int64_t eligible = capacityRegenerationProjectedHeal(perCreatureHeal);
	if(receivedHealth < 0 || receivedHealth > eligible)
		throw std::invalid_argument("Invalid capacity regeneration receipt");
	if(receivedHealth == 0)
		return 0;
	if(receivedHealth == eligible)
		return consumeCapacityRegeneration(perCreatureHeal); // Exact old allocation when nothing is prevented.
	const int32_t maximum = maximumPerCreature();
	int64_t remaining = receivedHealth;
	int32_t nextFirst = firstHPleft;
	if(nextFirst > 0)
	{
		const int32_t allowed = std::min(perCreatureHeal, maximum - nextFirst);
		const int32_t received = static_cast<int32_t>(std::min<int64_t>(remaining, allowed));
		nextFirst += received;
		remaining -= received;
	}
	std::vector<CapacityHealthCohort> updated;
	updated.reserve(capacityHealthCohorts.size() + 2);
	for(const auto & cohort : capacityHealthCohorts)
	{
		const int32_t allowed = std::min(perCreatureHeal, maximum - cohort.hitPoints);
		if(allowed <= 0 || remaining == 0)
		{
			updated.push_back(cohort);
			continue;
		}
		const int32_t whole = static_cast<int32_t>(std::min<int64_t>(cohort.count, remaining / allowed));
		if(whole > 0) updated.push_back({cohort.hitPoints + allowed, whole});
		remaining -= static_cast<int64_t>(whole) * allowed;
		int32_t untouched = cohort.count - whole;
		if(untouched > 0 && remaining > 0)
		{
			const int32_t received = static_cast<int32_t>(std::min<int64_t>(remaining, allowed));
			updated.push_back({cohort.hitPoints + received, 1});
			remaining -= received;
			--untouched;
		}
		if(untouched > 0) updated.push_back({cohort.hitPoints, untouched});
	}
	if(remaining != 0)
		throw std::logic_error("Capacity regeneration receipt exceeded survivor allowances");
	firstHPleft = nextFirst;
	capacityHealthCohorts = std::move(updated);
	normalizeCapacityHealth(true);
	return receivedHealth;
}
}
