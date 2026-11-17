/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/Rect.h"
#include "../../lib/battle/UnitShieldState.h"
#include <algorithm>
#include <string>

namespace shieldPresentation
{
struct Bar
{
	Rect bounds;
	int fillWidth = 0;
};

inline bool equal(const battle::ShieldSnapshot & lhs, const battle::ShieldSnapshot & rhs)
{
	for(size_t i = 0; i < lhs.pools.size(); ++i)
	{
		const auto & a = lhs.pools[i];
		const auto & b = rhs.pools[i];
		if(a.source != b.source || a.remaining != b.remaining || a.capacity != b.capacity
			|| a.physicalCreatureOnly != b.physicalCreatureOnly)
			return false;
	}
	return true;
}

/// One combined reservoir indicator, not a claim of universal damage coverage.
/// Use captured capacities while their sources remain active. Guardian Spirit
/// ends when exhausted; active Redemption can retain an empty, replenishable pool.
inline Bar bar(const battle::ShieldSnapshot & snapshot, Point amountPosition, int width)
{
	Bar result{Rect(amountPosition.x, amountPosition.y - 7, std::max(0, width), 2), 0};
	long double remaining = 0;
	long double capacity = 0;
	for(const auto & pool : snapshot.pools)
	{
		remaining += std::max<int64_t>(0, pool.remaining);
		capacity += std::max<int64_t>(0, pool.capacity);
	}
	if(remaining > 0 && capacity > 0 && width > 0)
	{
		const auto filled = std::min<long double>(width, remaining * width / capacity);
		result.fillWidth = std::clamp(static_cast<int>(filled), 1, width);
	}
	return result;
}

inline std::string tooltip(const battle::ShieldSnapshot & snapshot)
{
	std::string result;
	for(const auto & pool : snapshot.pools)
	{
		if(pool.capacity <= 0 && pool.remaining <= 0)
			continue;
		if(!result.empty())
			result += "\n\n";
		result += pool.source == battle::ShieldSource::GUARDIAN_SPIRIT
			? "Guardian Spirit" : "Vicarious Redemption";
		result += "\nShield HP remaining: " + std::to_string(pool.remaining)
			+ " / " + std::to_string(pool.capacity);
		result += pool.physicalCreatureOnly
			? "\nAbsorbs physical creature damage only."
			: "\nAbsorbs physical and magical damage.";
	}
	if(!result.empty())
		result += "\n\nWhite bar: combined remaining shield HP / original capacities of active shield sources."
			" Each source retains its own damage coverage; the combined amount is not universal protection.";
	return result;
}
}
