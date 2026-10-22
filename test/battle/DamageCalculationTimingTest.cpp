/*
 * DamageCalculationTimingTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/battle/DamageCalculationTiming.h"
#include <future>
#include <stdexcept>

namespace
{
namespace timing = damageCalculationTiming;
thread_local int64_t ticks = 0;
thread_local unsigned reads = 0;
timing::Clock::time_point now()
{
	++reads;
	return timing::Clock::time_point(std::chrono::nanoseconds(ticks));
}
}

TEST(DamageCalculationTimingTest, DisabledBindingDoesNotActivateInjectedClockOrSink)
{
	reads = 0;
	{
		timing::Binding disabled(nullptr, now);
		timing::Scope payload(timing::Phase::NATIVE_PAYLOAD);
		timing::noteCalculation(timing::Calculation::INITIAL);
		payload.finish();
	}
	EXPECT_EQ(reads, 0u);
}

TEST(DamageCalculationTimingTest, SequentialRegionsAndCountersAccumulateExactly)
{
	timing::Sample sample;
	reads = 0;
	ticks = 0;
	{
		timing::Binding enabled(&sample, now);
		timing::noteCalculation(timing::Calculation::INITIAL);
		{ timing::Scope stage(timing::Phase::NATIVE_PAYLOAD); ticks += 11; }
		{ timing::Scope stage(timing::Phase::BONUS_PREPARATION); ticks += 13; }
		{ timing::Scope stage(timing::Phase::LUA_CALL); ticks += 17; }
		timing::noteCalculation(timing::Calculation::LUCKY_SECOND);
		timing::noteCalculation(timing::Calculation::UNLUCKY_SECOND);
	}
	EXPECT_EQ(reads, 6u);
	EXPECT_EQ(sample.nativePayload.calls, 1u);
	EXPECT_EQ(sample.nativePayload.nanoseconds, 11u);
	EXPECT_EQ(sample.bonusPreparation.nanoseconds, 13u);
	EXPECT_EQ(sample.luaCall.nanoseconds, 17u);
	timing::Sample total;
	total.accumulate(sample);
	total.accumulate(sample);
	EXPECT_EQ(total.nativePayload.calls, 2u);
	EXPECT_EQ(total.nativePayload.nanoseconds, 22u);
	EXPECT_EQ(total.bonusPreparation.calls, 2u);
	EXPECT_EQ(total.bonusPreparation.nanoseconds, 26u);
	EXPECT_EQ(total.luaCall.calls, 2u);
	EXPECT_EQ(total.luaCall.nanoseconds, 34u);
	EXPECT_EQ(total.initialCalls, 2u);
	EXPECT_EQ(total.luckySecondCalls, 2u);
	EXPECT_EQ(total.unluckySecondCalls, 2u);
}

TEST(DamageCalculationTimingTest, NestedCallbacksStayInsideOuterRegionAndDoNotRebind)
{
	timing::Sample outer, nested;
	ticks = 0;
	reads = 0;
	{
		timing::Binding enabled(&outer, now);
		timing::Scope lua(timing::Phase::LUA_CALL);
		ticks += 10;
		{
			timing::Binding reentrant(&nested, now);
			timing::noteCalculation(timing::Calculation::INITIAL);
			timing::Scope payload(timing::Phase::NATIVE_PAYLOAD);
			ticks += 20;
		}
		ticks += 30;
	}
	EXPECT_EQ(reads, 2u);
	EXPECT_EQ(outer.luaCall.nanoseconds, 60u);
	EXPECT_EQ(outer.suppressedStages, 1u);
	EXPECT_EQ(outer.nativePayload.calls, 0u);
	EXPECT_EQ(outer.initialCalls, 0u);
	EXPECT_EQ(nested.luaCall.calls, 0u);
	EXPECT_EQ(nested.nativePayload.calls, 0u);
	EXPECT_EQ(nested.initialCalls, 0u);
}

TEST(DamageCalculationTimingTest, ExceptionsRestoreRoutingAndFinishOnlyOnce)
{
	timing::Sample sample;
	ticks = 0;
	reads = 0;
	EXPECT_THROW(( [&]
	{
		timing::Binding enabled(&sample, now);
		timing::Scope payload(timing::Phase::NATIVE_PAYLOAD);
		ticks += 7;
		payload.finish();
		payload.finish();
		throw std::runtime_error("diagnostic fixture");
	}() ), std::runtime_error);
	EXPECT_EQ(sample.nativePayload.calls, 1u);
	EXPECT_EQ(sample.nativePayload.nanoseconds, 7u);
	EXPECT_EQ(reads, 2u);
	{ timing::Scope disabled(timing::Phase::LUA_CALL); }
	EXPECT_EQ(reads, 2u);
}

TEST(DamageCalculationTimingTest, CandidateWorkersHaveIndependentThreadLocalSinks)
{
	auto worker = [](int64_t duration)
	{
		timing::Sample result;
		ticks = 0;
		timing::Binding enabled(&result, now);
		{ timing::Scope stage(timing::Phase::NATIVE_PAYLOAD); ticks += duration; }
		return result;
	};
	auto first = std::async(std::launch::async, worker, 31);
	auto second = std::async(std::launch::async, worker, 47);
	EXPECT_EQ(first.get().nativePayload.nanoseconds, 31u);
	EXPECT_EQ(second.get().nativePayload.nanoseconds, 47u);
}
