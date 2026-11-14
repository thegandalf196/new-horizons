/* DamageCacheLeadership.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "AttackPossibility.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include <limits>

namespace
{
int64_t boundedDamage(long double value)
{
	if(value >= static_cast<long double>(std::numeric_limits<int64_t>::max()))
		return std::numeric_limits<int64_t>::max();
	return static_cast<int64_t>(std::max(0.0L, value));
}
}

DamageCache::CachedDamage DamageCache::CachedDamage::capture(int64_t damage,
	const battle::Unit & actor, const CBattleInfoCallback & battle)
{
	CachedDamage result;
	result.count = actor.getCount();
	result.total = damage;
	result.perCreature = result.count > 0 ? static_cast<float>(damage) / result.count : 0;
	result.creature = actor.creatureId();
	const auto leadership = battle.battleLeadershipReadout(&actor);
	result.leadershipCapacity = leadership.active ? leadership.capacity : 0;
	return result;
}

bool DamageCache::CachedDamage::matchesLeadership(const battle::Unit & actor,
	const CBattleInfoCallback & battle) const
{
	if(creature != actor.creatureId())
		return false;
	const auto leadership = battle.battleLeadershipReadout(&actor);
	return leadershipCapacity == (leadership.active ? leadership.capacity : 0);
}

int64_t DamageCache::CachedDamage::atCount(int64_t requestedCount) const
{
	if(requestedCount <= 0 || count <= 0)
		return 0;
	if(leadershipCapacity == 0)
		return boundedDamage(perCreature * requestedCount);
	if(requestedCount == count)
		return total;
	if(requestedCount <= leadershipCapacity && count <= leadershipCapacity)
		return boundedDamage(perCreature * requestedCount);

	// Pair entries intentionally retain rounded damage and frozen modifiers.
	// Reuse that approximation, correcting only the nonlinear count contribution
	// through the same authoritative Leadership formula as actual damage.
	const auto effectiveCount = [this](int64_t amount)
	{
		return static_cast<long double>(amount)
			* newHorizonsHeroes::squareRootLeadershipEfficiency(amount, leadershipCapacity);
	};
	const long double scaled = static_cast<long double>(total)
		* effectiveCount(requestedCount) / effectiveCount(count);
	return boundedDamage(scaled);
}
