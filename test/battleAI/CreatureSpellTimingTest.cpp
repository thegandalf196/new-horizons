/*
 * CreatureSpellTimingTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../AI/BattleAI/CreatureSpellTiming.h"
#include <limits>
#include <stdexcept>

namespace
{
int64_t micros = 0;
unsigned reads = 0;
heroActionTiming::Clock::time_point now()
{
	++reads;
	return heroActionTiming::Clock::time_point(std::chrono::microseconds(micros));
}

void phase(creatureSpellTiming::Stage & stage, int64_t elapsed)
{
	creatureSpellTiming::Scope scope(&stage, now);
	micros += elapsed;
}

void pressure(creatureSpellTiming::Pressure & target)
{
	creatureSpellTiming::Scope total(&target.total, now);
	phase(target.cache, 10);
	phase(target.targets, 5);
	phase(target.targets, 5);
	micros += 10;
}
}

TEST(CreatureSpellTimingTest, DisabledAndCappedBudgetDoesNotReadTheClock)
{
	reads = 0;
	creatureSpellTiming::Budget budget;
	EXPECT_EQ(budget.take(false, 2).ordinal, 0u);
	EXPECT_EQ(budget.take(false, 2).ordinal, 0u);
	const auto first = budget.take(true, 2);
	const auto second = budget.take(true, 2);
	const auto cap = budget.take(true, 2);
	EXPECT_EQ(first.ordinal, 1u);
	EXPECT_TRUE(first.admitted);
	EXPECT_EQ(second.ordinal, 2u);
	EXPECT_TRUE(second.admitted);
	EXPECT_TRUE(cap.capReached);
	EXPECT_FALSE(cap.admitted);
	EXPECT_FALSE(budget.take(true, 2).capReached);
	{
		creatureSpellTiming::Scope disabled(nullptr, now);
	}
	EXPECT_EQ(reads, 0u);
}

TEST(CreatureSpellTimingTest, SequentialPartitionsAndNestedPressureAccountWithoutDoubleCounting)
{
	micros = 0;
	creatureSpellTiming::Sample sample;
	{
		creatureSpellTiming::Call call(&sample, now);
		phase(sample.baselineSetup, 10);
		pressure(sample.baseline);
		phase(sample.outcomeSetup, 10);
		phase(sample.outcomeCast, 10);
		pressure(sample.outcome);
		phase(sample.outcomeHealing, 5);
		phase(sample.ordinaryCast, 5);
		micros += 10;
	}
	EXPECT_EQ(sample.total.totalMicroseconds, 110u);
	EXPECT_EQ(sample.baseline.total.totalMicroseconds, 30u);
	EXPECT_EQ(sample.baseline.targets.calls, 2u);
	EXPECT_EQ(sample.baseline.targets.totalMicroseconds, 10u);
	EXPECT_EQ(sample.baseline.targets.maxMicroseconds, 5u);
	EXPECT_TRUE(creatureSpellTiming::accountingValid(sample));
	const auto fields = creatureSpellTiming::fields(sample);
	EXPECT_NE(fields.find("baseline_pressure_remainder_us=10"), std::string::npos);
	EXPECT_NE(fields.find(" remainder_us=10"), std::string::npos);
	sample.outcome.cache.totalMicroseconds = 31;
	EXPECT_FALSE(creatureSpellTiming::accountingValid(sample));
	EXPECT_NE(creatureSpellTiming::fields(sample).find("accounting_valid=0"), std::string::npos);
}

TEST(CreatureSpellTimingTest, UnwindingRetainsTheElapsedPhaseAndFailedAttempt)
{
	micros = 0;
	creatureSpellTiming::Sample sample;
	try
	{
		creatureSpellTiming::Call call(&sample, now);
		creatureSpellTiming::Scope phase(&sample.outcomeCast, now);
		micros = 23;
		throw std::runtime_error("cast forecast failure");
	}
	catch(const std::runtime_error &)
	{
	}
	EXPECT_TRUE(sample.threw);
	EXPECT_FALSE(sample.selected);
	EXPECT_EQ(sample.total.calls, 1u);
	EXPECT_EQ(sample.total.totalMicroseconds, 23u);
	EXPECT_EQ(sample.outcomeCast.totalMicroseconds, 23u);
	EXPECT_TRUE(creatureSpellTiming::accountingValid(sample));
}

TEST(CreatureSpellTimingTest, FixedNumericRowRetainsBranchIdentityAndIsBounded)
{
	creatureSpellTiming::Sample sample;
	sample.battle = 56;
	sample.player = 1;
	sample.unit = std::numeric_limits<int64_t>::max();
	sample.creature = 123;
	sample.randomPool = true;
	sample.selected = true;
	sample.selectedValueBits = std::numeric_limits<uint32_t>::max();
	for(auto * stage : {&sample.total, &sample.baselineSetup, &sample.outcomeSetup, &sample.outcomeCast,
		&sample.outcomeHealing, &sample.ordinaryCast, &sample.baseline.total, &sample.baseline.cache,
		&sample.baseline.targets, &sample.outcome.total, &sample.outcome.cache, &sample.outcome.targets})
		*stage = {999999999999999999ULL, 999999999999999999ULL, 999999999999999999ULL};
	const auto fields = creatureSpellTiming::fields(sample);
	EXPECT_NE(fields.find("branch=random_pool selected=1"), std::string::npos);
	EXPECT_NE(fields.find("unit=9223372036854775807 creature=123"), std::string::npos);
	EXPECT_LT(fields.size(), 4096u);
	sample.randomPool = false;
	EXPECT_NE(creatureSpellTiming::fields(sample).find("branch=ordinary"), std::string::npos);
}
