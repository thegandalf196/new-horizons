/*
 * DamageCacheTimingScope.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "AttackPossibility.h"
#include <chrono>
#include <exception>

namespace damageCacheDiagnostics
{
class DamageCacheOperationScope
{
	using Clock = std::chrono::steady_clock;
	DamageCacheProbeTiming * timing;
	bool outer = false;
	Clock::time_point started;
	uint64_t classifiedBefore = 0;
	int exceptionsBefore = 0;
public:
	explicit DamageCacheOperationScope(DamageCacheProbeTiming * receiver)
		: timing(receiver)
	{
		if(!timing)
			return;
		if(timing->phasesInFlight)
		{
			++timing->suppressedReentrantCalls;
			timing = nullptr;
			return;
		}
		outer = timing->operationsInFlight++ == 0;
		if(outer)
		{
			classifiedBefore = timing->preflight.totalNanoseconds + timing->dynamicDamage.totalNanoseconds;
			exceptionsBefore = std::uncaught_exceptions();
			started = Clock::now();
		}
	}
	DamageCacheOperationScope(const DamageCacheOperationScope &) = delete;
	DamageCacheOperationScope & operator=(const DamageCacheOperationScope &) = delete;
	DamageCacheProbeTiming * receiver() const { return timing; }
	~DamageCacheOperationScope()
	{
		if(!timing)
			return;
		--timing->operationsInFlight;
		if(!outer)
			return;
		const auto elapsed = Clock::now() - started;
		const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
		++timing->body.calls;
		timing->body.totalNanoseconds += nanoseconds;
		timing->body.totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		const auto classified = timing->preflight.totalNanoseconds + timing->dynamicDamage.totalNanoseconds
			- classifiedBefore;
		timing->remainderNanoseconds += nanoseconds - classified;
		if(std::uncaught_exceptions() > exceptionsBefore)
			++timing->failedOperations;
	}
};

class DamageCachePhaseScope
{
	using Clock = std::chrono::steady_clock;
	DamageCacheProbeTiming * timing;
	DamageCacheProbeTiming::StageTiming * stage;
	bool dynamic;
	Clock::time_point started;
	damageCalculationTiming::Binding payloadTiming;
public:
	DamageCachePhaseScope(DamageCacheProbeTiming * timing,
		DamageCacheProbeTiming::StageTiming DamageCacheProbeTiming::* member)
		: timing(timing), stage(timing ? &(timing->*member) : nullptr),
		  dynamic(member == &DamageCacheProbeTiming::dynamicDamage),
		  started(stage ? Clock::now() : Clock::time_point{}),
		  payloadTiming(dynamic && timing ? &timing->calculation : nullptr)
	{
		if(stage)
		{
			++timing->phasesInFlight;
			if(dynamic)
				++timing->dynamicCallbacksInFlight;
		}
	}
	DamageCachePhaseScope(const DamageCachePhaseScope &) = delete;
	DamageCachePhaseScope & operator=(const DamageCachePhaseScope &) = delete;
	~DamageCachePhaseScope() { finish(); }
	void finish()
	{
		if(!stage)
			return;
		const auto elapsed = Clock::now() - started;
		++stage->calls;
		stage->totalNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
		stage->totalMicroseconds += std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
		--timing->phasesInFlight;
		if(dynamic)
			--timing->dynamicCallbacksInFlight;
		stage = nullptr;
	}
};

}
