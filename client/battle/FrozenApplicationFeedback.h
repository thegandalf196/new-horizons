/*
 * FrozenApplicationFeedback.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include <cstdint>
#include <map>

namespace newHorizonsFrozenPresentation
{
/// Presentation-only deduplication; never changes a unit or its saved marker.
class ApplicationFeedback
{
	std::map<uint32_t, int32_t> lastApplicationRound;

public:
	void seed(uint32_t unit, int32_t round)
	{
		(void)record(unit, round);
	}

	bool record(uint32_t unit, int32_t round)
	{
		if(round < 0)
			return false;
		const auto [entry, inserted] = lastApplicationRound.try_emplace(unit, round);
		if(inserted)
			return true;
		if(round <= entry->second)
			return false;
		entry->second = round;
		return true;
	}
};
}
