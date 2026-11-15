/* New Horizons profiling controls. GPL-2.0-or-later. */
#include "StdInc.h"
#include "../../lib/network/InitializationProfile.h"
#include "../../lib/network/PerformanceTrace.h"
#include <atomic>

namespace
{
using nhLoadProfile::Accumulator;
using nhLoadProfile::Metric;
const nhLoadProfile::Sample & sample(const Accumulator & data, Metric metric)
{
	return data.samples[static_cast<size_t>(metric)];
}
}

TEST(InitializationProfileTest, NestedInclusiveAndExclusiveTimesAreNotDoubleCounted)
{
	Accumulator data;
	ASSERT_TRUE(data.enter(Metric::WorldInit, 10));
	ASSERT_TRUE(data.enter(Metric::PerkValidation, 20));
	data.leave(50);
	data.leave(110);
	EXPECT_TRUE(data.empty());
	EXPECT_EQ(0, data.dropped);
	EXPECT_EQ(1, sample(data, Metric::WorldInit).calls);
	EXPECT_EQ(100, sample(data, Metric::WorldInit).inclusiveNs);
	EXPECT_EQ(70, sample(data, Metric::WorldInit).exclusiveNs);
	EXPECT_EQ(30, sample(data, Metric::PerkValidation).inclusiveNs);
	EXPECT_EQ(30, sample(data, Metric::PerkValidation).exclusiveNs);
}

TEST(InitializationProfileTest, ReentrantMetricCountsBothCallsButExclusivePartitionsTime)
{
	Accumulator data;
	ASSERT_TRUE(data.enter(Metric::PerkValidation, 0));
	ASSERT_TRUE(data.enter(Metric::PerkValidation, 20));
	data.leave(50);
	data.leave(100);
	EXPECT_EQ(2, sample(data, Metric::PerkValidation).calls);
	EXPECT_EQ(130, sample(data, Metric::PerkValidation).inclusiveNs);
	EXPECT_EQ(100, sample(data, Metric::PerkValidation).exclusiveNs);
}

TEST(InitializationProfileTest, FixedDepthOverflowInvalidatesMeasurementWithoutLosingParents)
{
	Accumulator data;
	for(uint64_t i = 0; i < 64; ++i) ASSERT_TRUE(data.enter(Metric::StartingPerks, i));
	EXPECT_FALSE(data.enter(Metric::StartingPerks, 64));
	for(unsigned i = 0; i < 64; ++i) data.leave(100);
	EXPECT_TRUE(data.empty());
	EXPECT_EQ(1, data.dropped);
	EXPECT_EQ(64, sample(data, Metric::StartingPerks).calls);
	EXPECT_EQ(100, sample(data, Metric::StartingPerks).exclusiveNs);
}

TEST(InitializationProfileTest, InvalidClockOrUnbalancedLeaveIsVisible)
{
	Accumulator data;
	ASSERT_TRUE(data.enter(Metric::StartingPerks, 10));
	data.leave(9);
	data.leave(20);
	EXPECT_EQ(2, data.dropped);
	EXPECT_EQ(0, sample(data, Metric::StartingPerks).calls);
	EXPECT_TRUE(data.empty());
}

TEST(InitializationProfileTest, EmptyScopeOverheadControlReportsRatherThanAssumesCost)
{
	constexpr unsigned iterations = 100000;
	const auto baselineStart = nhperf::nowNs();
	for(unsigned i = 0; i < iterations; ++i) std::atomic_signal_fence(std::memory_order_seq_cst);
	const auto baselineNs = nhperf::nowNs() - baselineStart;
	nhLoadProfile::Phase phase("empty-scope-overhead-control");
	const bool enabled = nhLoadProfile::active();
	const auto measuredStart = nhperf::nowNs();
	for(unsigned i = 0; i < iterations; ++i)
	{
		nhLoadProfile::Scope scope(Metric::PerkDefinition);
		std::atomic_signal_fence(std::memory_order_seq_cst);
	}
	const auto measuredNs = nhperf::nowNs() - measuredStart;
	RecordProperty("iterations", iterations);
	RecordProperty("profile_active", enabled ? 1 : 0);
	RecordProperty("empty_loop_ns", std::to_string(baselineNs));
	RecordProperty("scope_loop_ns", std::to_string(measuredNs));
	EXPECT_GT(measuredNs, 0);
}

TEST(InitializationProfileTest, DerivedValuesAreNotEvaluatedOutsideAnEnabledPhase)
{
	unsigned evaluations = 0;
	const auto derived = [&]() { ++evaluations; return uint64_t{42}; };
	if(nhLoadProfile::active())
		nhLoadProfile::count(Metric::ProfileRows, derived());
	EXPECT_EQ(0, evaluations);
	{
		nhLoadProfile::Phase phase("derived-value-control");
		const bool enabled = nhLoadProfile::active();
		if(nhLoadProfile::active())
			nhLoadProfile::count(Metric::ProfileRows, derived());
		EXPECT_EQ(enabled ? 1 : 0, evaluations);
	}
}
