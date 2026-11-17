/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <chrono>
#include <utility>
namespace battleSelectionTiming
{
template<typename StageTiming>
class SelectionStageScope
{
	using Clock = std::chrono::steady_clock;
	StageTiming * timing;
	StageTiming * aggregate;
	Clock::time_point started;

public:
	explicit SelectionStageScope(StageTiming * timing, StageTiming * aggregate = nullptr)
		: timing(timing), aggregate(aggregate), started(timing ? Clock::now() : Clock::time_point{})
	{}

	~SelectionStageScope()
	{
		if(timing)
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
				Clock::now() - started).count();
			++timing->calls;
			timing->totalMicroseconds += elapsed;
			if(aggregate)
			{
				++aggregate->calls;
				aggregate->totalMicroseconds += elapsed;
			}
		}
	}
};

template<typename StageTiming, typename Callable>
decltype(auto) measureSelectionStage(StageTiming * timing,
	Callable && action, StageTiming * aggregate = nullptr)
{
	SelectionStageScope measurement(timing, aggregate);
	return std::forward<Callable>(action)();
}
}
