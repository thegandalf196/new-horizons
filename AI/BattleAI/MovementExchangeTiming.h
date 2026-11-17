/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "BattleExchangeVariant.h"
#include <chrono>
namespace battleSearchTiming
{
class MovementExchangeScope
{
	using Clock = std::chrono::steady_clock;
	BattleMovementTiming::StageTiming * timing;
	Clock::time_point started;
public:
	explicit MovementExchangeScope(BattleMovementTiming::StageTiming * timing)
		: timing(timing), started(timing ? Clock::now() : Clock::time_point{})
	{}
	~MovementExchangeScope()
	{
		finish();
	}
	void finish()
	{
		if(timing)
		{
			++timing->calls;
			timing->totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(
				Clock::now() - started).count();
			timing = nullptr;
		}
	}
};
}
