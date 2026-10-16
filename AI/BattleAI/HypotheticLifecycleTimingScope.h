/*
 * HypotheticLifecycleTimingScope.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "HypotheticBattleTypes.h"

#include <chrono>

namespace hypotheticLifecycleDiagnostics
{
class LifecycleTimingScope
{
	using Clock = std::chrono::steady_clock;
	HypotheticLifecycleTiming::StageTiming * timing;
	Clock::time_point started;
public:
	explicit LifecycleTimingScope(HypotheticLifecycleTiming::StageTiming * timing)
		: timing(timing), started(timing ? Clock::now() : Clock::time_point{})
	{}
	~LifecycleTimingScope()
	{
		if(timing)
		{
			++timing->calls;
			const auto elapsed = Clock::now() - started;
			timing->totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
			timing->totalNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
		}
	}
};
}
