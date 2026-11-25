/*
 * UnitHotStatsQuery.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "Unit.h"

namespace battle
{
namespace hotStats
{
/// Checker mode only: counts a verified reuse; aborts with a diagnostic when
/// the reused answer differs from a fresh computation under the same key.
DLL_LINKAGE void verify(const Unit & unit, HotStat stat, uint32_t key, int32_t reused, int32_t fresh);
/// Checker mode only: counts a computed answer, for the exit report's hit rate.
DLL_LINKAGE void noteComputed(HotStat stat);
}

/// `compute()` is the slot's only definition. The answer is reused while the
/// unit's validity key is unchanged and recomputed after any change of it.
template<typename Compute>
int32_t hotStat(const Unit & unit, HotStat stat, Compute && compute)
{
	const UnitHotStatsAccess access = unit.hotStatsAccess();
	if(!access.stats || (access.excluded & hotStatBit(stat)) != 0)
		return static_cast<int32_t>(compute());
	int32_t reused = 0;
	if(access.stats->find(stat, access.key, reused))
	{
		if(access.stats->mode() == UnitHotStats::Mode::CHECK)
		{
			const auto fresh = static_cast<int32_t>(compute());
			// Compare only answers for one key; a computation may advance it.
			if(unit.hotStatsAccess().key == access.key)
				hotStats::verify(unit, stat, access.key, reused, fresh);
		}
		return reused;
	}
	const auto value = static_cast<int32_t>(compute());
	if(access.stats->mode() == UnitHotStats::Mode::CHECK)
		hotStats::noteComputed(stat);
	// Store only an answer that is still current for the key it was computed
	// under: the computation itself may change the unit or its key.
	const UnitHotStatsAccess after = unit.hotStatsAccess();
	if(after.stats == access.stats && after.key == access.key)
		access.stats->store(stat, access.key, value);
	return value;
}
}
