/*
 * DamageCalculationTimingTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/battle/DamageCalculationTiming.h"
#include <array>
#include <future>
#include <stdexcept>

namespace
{
namespace timing = damageCalculationTiming;
constexpr unsigned nativeRegionCount = 4;
std::array<timing::Stage *, nativeRegionCount> nativeStages(timing::Sample & sample)
{
	return {&sample.nativeAttackContext, &sample.nativePhysicalModifiers,
		&sample.nativeHeroOrders, &sample.nativeFinalization};
}
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
		payload.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS);
		payload.nextNativeRegion(timing::NativeRegion::HERO_ORDERS);
		payload.nextNativeRegion(timing::NativeRegion::FINALIZATION);
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

TEST(DamageCalculationTimingTest, CompletedNativeRegionsShareBoundaryReadingsAndCloseExactly)
{
	timing::Sample sample;
	reads = 0;
	ticks = 0;
	{
		timing::Binding enabled(&sample, now);
		timing::noteCalculation(timing::Calculation::INITIAL);
		timing::Scope payload(timing::Phase::NATIVE_PAYLOAD);
		ticks += 11;
		payload.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS);
		ticks += 13;
		payload.nextNativeRegion(timing::NativeRegion::HERO_ORDERS);
		ticks += 17;
		payload.nextNativeRegion(timing::NativeRegion::FINALIZATION);
		ticks += 19;
	}
	EXPECT_EQ(reads, 5u); // Start + three shared boundaries + shared finish.
	EXPECT_EQ(sample.nativePayload.calls, 1u);
	EXPECT_EQ(sample.nativePayload.nanoseconds, 60u);
	EXPECT_EQ(sample.initialCalls, 1u);
	EXPECT_EQ(sample.nativeAttackContext.nanoseconds, 11u);
	EXPECT_EQ(sample.nativePhysicalModifiers.nanoseconds, 13u);
	EXPECT_EQ(sample.nativeHeroOrders.nanoseconds, 17u);
	EXPECT_EQ(sample.nativeFinalization.nanoseconds, 19u);
	uint64_t sum = 0;
	for(const auto * region : nativeStages(sample))
	{
		EXPECT_EQ(region->calls, sample.nativePayload.calls);
		sum += region->nanoseconds;
	}
	EXPECT_EQ(sum, sample.nativePayload.nanoseconds);
}

TEST(DamageCalculationTimingTest, SuppressedAndNonNativeTransitionsDoNotReadClock)
{
	timing::Sample sample, nested;
	reads = 0;
	ticks = 0;
	{
		timing::Binding enabled(&sample, now);
		{
			timing::Scope payload(timing::Phase::NATIVE_PAYLOAD);
			ticks += 3;
			payload.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS);
			{
				timing::Binding ignored(&nested, now);
				timing::Scope suppressed(timing::Phase::NATIVE_PAYLOAD);
				suppressed.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS);
				suppressed.nextNativeRegion(timing::NativeRegion::HERO_ORDERS);
				suppressed.nextNativeRegion(timing::NativeRegion::FINALIZATION);
				ticks += 5;
			}
			payload.nextNativeRegion(timing::NativeRegion::HERO_ORDERS);
			++ticks;
			payload.nextNativeRegion(timing::NativeRegion::FINALIZATION);
			++ticks;
		}
		EXPECT_EQ(reads, 5u);
		{
			timing::Scope preparation(timing::Phase::BONUS_PREPARATION);
			preparation.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS);
			preparation.nextNativeRegion(timing::NativeRegion::HERO_ORDERS);
			preparation.nextNativeRegion(timing::NativeRegion::FINALIZATION);
			ticks += 7;
		}
	}
	EXPECT_EQ(reads, 7u);
	EXPECT_EQ(sample.suppressedStages, 1u);
	EXPECT_EQ(sample.nativePayload.nanoseconds, 10u);
	EXPECT_EQ(sample.bonusPreparation.nanoseconds, 7u);
	for(unsigned index = 0; index < nativeRegionCount; ++index)
	{
		EXPECT_EQ(nativeStages(sample)[index]->calls, 1u);
		EXPECT_EQ(nativeStages(nested)[index]->calls, 0u);
	}
	EXPECT_EQ(nested.nativePayload.calls, 0u);
	EXPECT_EQ(sample.nativePhysicalModifiers.nanoseconds, 5u);
}

TEST(DamageCalculationTimingTest, NativeExceptionsCloseOnlyReachedRegions)
{
	for(unsigned reached = 0; reached < nativeRegionCount; ++reached)
	{
		SCOPED_TRACE(reached);
		timing::Sample sample;
		reads = 0;
		ticks = 0;
		EXPECT_THROW(( [&]
		{
			timing::Binding enabled(&sample, now);
			timing::Scope payload(timing::Phase::NATIVE_PAYLOAD);
			for(unsigned step = 0; step < reached; ++step)
			{
				ticks += step + 1;
				payload.nextNativeRegion(static_cast<timing::NativeRegion>(step + 1));
			}
			ticks += 10;
			throw std::runtime_error("native region fixture");
		}() ), std::runtime_error);
		EXPECT_EQ(reads, reached + 2);
		EXPECT_EQ(sample.nativePayload.calls, 1u);
		uint64_t sum = 0;
		for(unsigned index = 0; index < nativeRegionCount; ++index)
		{
			const auto & region = *nativeStages(sample)[index];
			EXPECT_EQ(region.calls, index <= reached ? 1u : 0u);
			if(index > reached)
				EXPECT_EQ(region.nanoseconds, 0u);
			sum += region.nanoseconds;
		}
		EXPECT_EQ(sum, sample.nativePayload.nanoseconds);
		EXPECT_EQ(sample.nativePayload.nanoseconds, static_cast<uint64_t>(ticks));
	}
}

TEST(DamageCalculationTimingTest, InvalidAndFinishedNativeTransitionsCannotReopenOrReadClock)
{
	timing::Sample sample;
	reads = 0;
	ticks = 0;
	timing::Binding enabled(&sample, now);
	timing::Scope payload(timing::Phase::NATIVE_PAYLOAD);
	payload.nextNativeRegion(timing::NativeRegion::HERO_ORDERS); // Cannot skip a region.
	EXPECT_EQ(reads, 1u);
	payload.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS);
	payload.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS); // Cannot count twice.
	EXPECT_EQ(reads, 2u);
	payload.nextNativeRegion(timing::NativeRegion::HERO_ORDERS);
	payload.nextNativeRegion(timing::NativeRegion::FINALIZATION);
	++ticks;
	payload.finish();
	payload.nextNativeRegion(timing::NativeRegion::PHYSICAL_MODIFIERS);
	payload.finish();
	EXPECT_EQ(reads, 5u);
	EXPECT_EQ(sample.nativePayload.calls, 1u);
	EXPECT_EQ(sample.nativePayload.nanoseconds, 1u);
	for(const auto * region : nativeStages(sample))
		EXPECT_EQ(region->calls, 1u);
}

TEST(DamageCalculationTimingTest, NativeRegionSamplesJoinEveryFieldAndRetainZeroSlots)
{
	timing::Sample first, second, zero, total;
	uint64_t firstSum = 0, secondSum = 0;
	for(unsigned index = 0; index < nativeRegionCount; ++index)
	{
		auto & a = *nativeStages(first)[index];
		auto & b = *nativeStages(second)[index];
		a.calls = 1;
		a.nanoseconds = index + 1;
		b.calls = 2;
		b.nanoseconds = 10 * (index + 1);
		firstSum += a.nanoseconds;
		secondSum += b.nanoseconds;
	}
	first.nativePayload = {1, firstSum};
	second.nativePayload = {2, secondSum};
	total.accumulate(first);
	total.accumulate(zero);
	total.accumulate(second);
	uint64_t sum = 0;
	for(unsigned index = 0; index < nativeRegionCount; ++index)
	{
		const auto & region = *nativeStages(total)[index];
		EXPECT_EQ(region.calls, 3u);
		EXPECT_EQ(region.nanoseconds, 11u * (index + 1));
		sum += region.nanoseconds;
	}
	EXPECT_EQ(total.nativePayload.calls, 3u);
	EXPECT_EQ(sum, total.nativePayload.nanoseconds);
	EXPECT_EQ(total.bonusPreparation.calls, 0u);
	EXPECT_EQ(total.luaCall.calls, 0u);
}
