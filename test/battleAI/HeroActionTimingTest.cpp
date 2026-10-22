/*
 * HeroActionTimingTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../AI/BattleAI/HeroActionTiming.h"
#include "HeroActionTimingCapture.h"

#include <tbb/parallel_for.h>
#include <atomic>
#include <stdexcept>

namespace
{
thread_local int64_t clockMicroseconds = 0;
thread_local unsigned clockReads = 0;

heroActionTiming::Clock::time_point fakeNow()
{
	++clockReads;
	return heroActionTiming::Clock::time_point(std::chrono::microseconds(clockMicroseconds));
}
}

TEST(HeroActionTimingTest, DisabledScopesReadNoClockAndPrepareNoCandidateStorage)
{
	clockReads = 0;
	heroActionTiming::Candidates candidates;
	candidates.prepare(false, 999999);
	{
		heroActionTiming::Scope continuation(nullptr, fakeNow);
		heroActionTiming::Scope setup(nullptr, fakeNow);
		heroActionTiming::Scope reachability(nullptr, fakeNow);
		heroActionTiming::Scope movement(nullptr, fakeNow);
		heroActionTiming::Scope bestTarget(nullptr, fakeNow);
		heroActionTiming::Scope candidate(nullptr, fakeNow);
		heroActionTiming::LoopScope loop(candidates, nullptr, fakeNow);
		heroActionTiming::SubmitScope submit(nullptr, fakeNow);
	}
	EXPECT_EQ(clockReads, 0u);
	EXPECT_EQ(candidates.size(), 0u);
	EXPECT_EQ(candidates.at(0), nullptr);
	EXPECT_TRUE(candidates.types().empty());
}

TEST(HeroActionTimingTest, CandidateTypesKeepOrderAndSpellKeysSeparateAndReduceExactTimes)
{
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 4);
	for(size_t index = 0; index < 3; ++index)
	{
		auto * slot = candidates.at(index);
		slot->command = index == 1 ? 0 : 7;
		slot->spell = index == 1 ? 7 : -1;
		clockMicroseconds = 100;
		{
			heroActionTiming::Scope elapsed(&slot->candidate, fakeNow);
			clockMicroseconds += static_cast<int64_t>(index + 1) * 10;
		}
		slot->full = {1, index + 1, index + 1};
		slot->cached = {1, index + 2, index + 2};
		slot->fullMovement = {1, index + 3, index + 3};
		slot->fullBestTarget = {1, index + 4, index + 4};
	}
	const auto types = candidates.types();
	ASSERT_EQ(types.size(), 2u);
	EXPECT_EQ(types[0].command, 7);
	EXPECT_EQ(types[0].spell, -1);
	EXPECT_EQ(types[0].candidate.calls, 2u);
	EXPECT_EQ(types[0].candidate.totalMicroseconds, 40u);
	EXPECT_EQ(types[0].candidate.maxMicroseconds, 30u);
	EXPECT_EQ(types[0].full.totalMicroseconds, 4u);
	EXPECT_EQ(types[0].cached.totalMicroseconds, 6u);
	EXPECT_EQ(types[0].movement.totalMicroseconds, 8u);
	EXPECT_EQ(types[0].bestTarget.totalMicroseconds, 10u);
	EXPECT_EQ(types[1].command, 0);
	EXPECT_EQ(types[1].spell, 7);
	EXPECT_EQ(types[1].candidate.calls, 1u);
	EXPECT_EQ(types[1].candidate.totalMicroseconds, 20u);
	// Slot3 was never entered; it must not fabricate a measured candidate.
	EXPECT_EQ(types[0].candidate.calls + types[1].candidate.calls, 3u);
}

TEST(HeroActionTimingTest, CandidateTypeBudgetKeepsExactOverflowAndExistingGroups)
{
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 68);
	for(size_t index = 0; index < 68; ++index)
	{
		auto * slot = candidates.at(index);
		slot->command = 0;
		slot->spell = index == 67 ? 0 : static_cast<int>(index);
		slot->candidate = {1, 10, 10};
		slot->fullMovement = {1, 3, 3};
	}
	const auto types = candidates.types();
	ASSERT_EQ(types.size(), 65u);
	EXPECT_EQ(types[0].spell, 0);
	EXPECT_EQ(types[0].candidate.calls, 2u);
	EXPECT_EQ(types.back().command, -1);
	EXPECT_EQ(types.back().spell, -1);
	EXPECT_EQ(types.back().candidate.calls, 3u);
	EXPECT_EQ(types.back().candidate.totalMicroseconds, 30u);
	EXPECT_EQ(types.back().movement.totalMicroseconds, 9u);
	uint64_t calls = 0;
	for(const auto & type : types)
		calls += type.candidate.calls;
	EXPECT_EQ(calls, 68u);
}

TEST(HeroActionTimingTest, CandidateTypeSchemaHasExplicitIdentityCountsAndNestedSums)
{
	heroActionTiming::TypeSummary type;
	type.command = 10;
	type.candidate = {3, 210, 90};
	type.full = {2, 100, 60};
	type.cached = {1, 20, 20};
	type.movement = {2, 70, 40};
	type.bestTarget = {2, 10, 7};
	EXPECT_EQ(heroActionTiming::typeFields(type),
		" command=10 spell=-1 overflow=0 evaluations=3 sum_us=210 max_us=90"
		" full_calls=2 full_sum_us=100 cached_calls=1 cached_sum_us=20"
		" movement_sum_us=70 best_target_sum_us=10");
	const heroActionTiming::TypeSummary overflow;
	EXPECT_TRUE(heroActionTiming::typeFields(overflow).starts_with(" command=-1 spell=-1 overflow=1 "));
}

TEST(HeroActionTimingTest, CandidateExceptionScopeStillRecordsElapsedAndJoinedFailure)
{
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 1);
	heroActionTiming::Summary summary;
	clockMicroseconds = 100;
	try
	{
		heroActionTiming::LoopScope loop(candidates, &summary, fakeNow);
		heroActionTiming::Scope candidate(&candidates.at(0)->candidate, fakeNow);
		candidates.at(0)->command = 2;
		clockMicroseconds = 123;
		throw std::runtime_error("candidate fixture");
	}
	catch(const std::runtime_error &) {}
	ASSERT_TRUE(summary.loopJoined);
	ASSERT_TRUE(summary.loopThrew);
	const auto types = candidates.types();
	ASSERT_EQ(types.size(), 1u);
	EXPECT_EQ(types.front().candidate.calls, 1u);
	EXPECT_EQ(types.front().candidate.totalMicroseconds, 23u);
}

TEST(HeroActionTimingTest, MissingStackIdentityIsExplicitAndDoesNotDereferenceNull)
{
	const heroActionTiming::Identity identity;
	EXPECT_EQ(identity.stack, -1);
	EXPECT_EQ(heroActionTiming::captureStackId(nullptr), -1);
}

TEST(HeroActionTimingTest, ScopeAccumulatesCallsSumAndMaximumUsingOneElapsedSample)
{
	heroActionTiming::Stage stage;
	clockReads = 0;
	clockMicroseconds = 10;
	{
		heroActionTiming::Scope scope(&stage, fakeNow);
		clockMicroseconds = 27;
	}
	clockMicroseconds = 30;
	{
		heroActionTiming::Scope scope(&stage, fakeNow);
		clockMicroseconds = 33;
	}
	EXPECT_EQ(clockReads, 4u);
	EXPECT_EQ(stage.calls, 2u);
	EXPECT_EQ(stage.totalMicroseconds, 20u);
	EXPECT_EQ(stage.maxMicroseconds, 17u);
}

TEST(HeroActionTimingTest, CandidateReductionKeepsOverlappingSumsSeparateFromLoopWall)
{
	heroActionTiming::Summary summary;
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 3);
	clockMicroseconds = 100;
	{
		heroActionTiming::LoopScope loop(candidates, &summary, fakeNow);
		candidates.at(0)->full = {1, 60, 60};
		candidates.at(1)->full = {1, 80, 80};
		candidates.at(2)->cached = {1, 25, 25};
		candidates.at(0)->fullSetup = {1, 5, 5};
		candidates.at(1)->fullSetup = {1, 7, 7};
		candidates.at(0)->fullReachability = {1, 10, 10};
		candidates.at(1)->fullReachability = {1, 13, 13};
		candidates.at(0)->fullMovement = {1, 20, 20};
		candidates.at(1)->fullMovement = {1, 23, 23};
		candidates.at(1)->fullBestTarget = {1, 29, 29};
		candidates.at(0)->fullBestTargetDetails.waitedExchange = {2, 17};
		candidates.at(1)->fullBestTargetDetails.waitedExchange = {3, 19};
		candidates.at(0)->fullBestTargetDetails.waitedDetails.appliedAttacks = {4, 11};
		candidates.at(1)->fullBestTargetDetails.normalDetails.body = {5, 13};
		candidates.at(1)->fullBestTargetDetails.normalDetails.remainderMicroseconds = 7;
		candidates.at(0)->fullMovementDetails.findMoveExchange = {2, 31};
		candidates.at(1)->fullMovementDetails.findMoveExchange = {3, 37};
		candidates.at(0)->fullMovementDetails.findMoveExchangeDetails.body = {2, 29};
		candidates.at(1)->fullMovementDetails.findMoveExchangeDetails.body = {3, 35};
		candidates.at(1)->fullMovementDetails.findMoveExchangeDetails.appliedAttacks = {4, 17};
		candidates.at(1)->fullMovementDetails.findMoveExchangeDetails.remainderMicroseconds = 5;
		// Workers' wall intervals may overlap: their sum can exceed loop wall.
		clockMicroseconds = 200;
		EXPECT_EQ(summary.continuation.full.calls, 0u);
		EXPECT_FALSE(summary.loopJoined);
	}
	EXPECT_TRUE(summary.loopJoined);
	EXPECT_FALSE(summary.loopThrew);
	EXPECT_EQ(summary.candidates, 3u);
	EXPECT_EQ(summary.candidateLoop.calls, 1u);
	EXPECT_EQ(summary.candidateLoop.totalMicroseconds, 100u);
	EXPECT_EQ(summary.continuation.full.calls, 2u);
	EXPECT_EQ(summary.continuation.full.totalMicroseconds, 140u);
	EXPECT_EQ(summary.continuation.full.maxMicroseconds, 80u);
	EXPECT_EQ(summary.continuation.cached.calls, 1u);
	EXPECT_EQ(summary.continuation.cached.totalMicroseconds, 25u);
	EXPECT_EQ(summary.continuation.fullSetup.calls, 2u);
	EXPECT_EQ(summary.continuation.fullSetup.totalMicroseconds, 12u);
	EXPECT_EQ(summary.continuation.fullSetup.maxMicroseconds, 7u);
	EXPECT_EQ(summary.continuation.fullReachability.calls, 2u);
	EXPECT_EQ(summary.continuation.fullReachability.totalMicroseconds, 23u);
	EXPECT_EQ(summary.continuation.fullReachability.maxMicroseconds, 13u);
	EXPECT_EQ(summary.continuation.fullMovement.calls, 2u);
	EXPECT_EQ(summary.continuation.fullMovement.totalMicroseconds, 43u);
	EXPECT_EQ(summary.continuation.fullMovement.maxMicroseconds, 23u);
	EXPECT_EQ(summary.continuation.fullBestTarget.calls, 1u);
	EXPECT_EQ(summary.continuation.fullBestTarget.totalMicroseconds, 29u);
	EXPECT_EQ(summary.continuation.fullBestTarget.maxMicroseconds, 29u);
	EXPECT_EQ(summary.continuation.fullBestTargetDetails.waitedExchange.calls, 5u);
	EXPECT_EQ(summary.continuation.fullBestTargetDetails.waitedExchange.totalMicroseconds, 36u);
	EXPECT_EQ(summary.continuation.fullBestTargetDetails.waitedDetails.appliedAttacks.calls, 4u);
	EXPECT_EQ(summary.continuation.fullBestTargetDetails.normalDetails.body.totalMicroseconds, 13u);
	EXPECT_EQ(summary.continuation.fullBestTargetDetails.normalDetails.remainderMicroseconds, 7u);
	const auto & movement = summary.continuation.fullMovementDetails;
	EXPECT_EQ(movement.findMoveExchange.calls, 5u);
	EXPECT_EQ(movement.findMoveExchange.totalMicroseconds, 68u);
	EXPECT_EQ(movement.findMoveExchangeDetails.body.calls, 5u);
	EXPECT_EQ(movement.findMoveExchangeDetails.body.totalMicroseconds, 64u);
	EXPECT_EQ(movement.findMoveExchangeDetails.appliedAttacks.calls, 4u);
	EXPECT_EQ(movement.findMoveExchangeDetails.appliedAttacks.totalMicroseconds, 17u);
	EXPECT_EQ(movement.findMoveExchangeDetails.remainderMicroseconds, 5u);
}

TEST(HeroActionTimingTest, ParallelWorkersOwnDistinctSlotsAndReduceOnlyAfterJoin)
{
	heroActionTiming::Summary summary;
	heroActionTiming::Candidates candidates;
	constexpr size_t count = 64;
	candidates.prepare(true, count);
	{
		heroActionTiming::LoopScope loop(candidates, &summary);
		tbb::parallel_for(size_t{0}, count, [&](size_t index)
		{
			clockMicroseconds = 10;
			heroActionTiming::Scope scope(&candidates.at(index)->full, fakeNow);
			heroActionTiming::Scope setup(&candidates.at(index)->fullSetup, fakeNow);
			candidates.at(index)->fullBestTargetDetails.normalDetails.body = {1, index};
			candidates.at(index)->fullMovementDetails.findMoveExchange = {1, index + 1};
			candidates.at(index)->fullMovementDetails.findMoveExchangeDetails.body = {1, index};
			clockMicroseconds = 17;
		});
		EXPECT_EQ(summary.continuation.full.calls, 0u);
	}
	EXPECT_EQ(summary.candidates, count);
	EXPECT_EQ(summary.continuation.full.calls, count);
	EXPECT_EQ(summary.continuation.full.totalMicroseconds, count * 7);
	EXPECT_EQ(summary.continuation.full.maxMicroseconds, 7u);
	EXPECT_EQ(summary.continuation.fullSetup.calls, count);
	EXPECT_EQ(summary.continuation.fullSetup.totalMicroseconds, count * 7);
	EXPECT_EQ(summary.continuation.fullSetup.maxMicroseconds, 7u);
	EXPECT_EQ(summary.continuation.cached.calls, 0u);
	EXPECT_EQ(summary.continuation.fullBestTargetDetails.normalDetails.body.calls, count);
	EXPECT_EQ(summary.continuation.fullBestTargetDetails.normalDetails.body.totalMicroseconds, count * (count - 1) / 2);
	EXPECT_EQ(summary.continuation.fullMovementDetails.findMoveExchange.calls, count);
	EXPECT_EQ(summary.continuation.fullMovementDetails.findMoveExchange.totalMicroseconds, count * (count + 1) / 2);
	EXPECT_EQ(summary.continuation.fullMovementDetails.findMoveExchangeDetails.body.calls, count);
	EXPECT_EQ(summary.continuation.fullMovementDetails.findMoveExchangeDetails.body.totalMicroseconds, count * (count - 1) / 2);
}

TEST(HeroActionTimingTest, RejectedAndDeadCandidatesNeedNoContinuationSample)
{
	heroActionTiming::Summary summary;
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 2);
	candidates.finish(summary, false);
	EXPECT_EQ(summary.candidates, 2u);
	EXPECT_EQ(summary.continuation.full.calls, 0u);
	EXPECT_EQ(summary.continuation.cached.calls, 0u);
}

TEST(HeroActionTimingTest, SequentialFullChildrenKeepElapsedSeparateAndFinalizeOnThrow)
{
	heroActionTiming::Continuation timing;
	clockMicroseconds = 0;
	EXPECT_THROW({
		heroActionTiming::Scope full(&timing.full, fakeNow);
		{
			heroActionTiming::Scope setup(&timing.fullSetup, fakeNow);
			clockMicroseconds = 5;
		}
		{
			heroActionTiming::Scope reachability(&timing.fullReachability, fakeNow);
			clockMicroseconds = 12;
		}
		{
			heroActionTiming::Scope movement(&timing.fullMovement, fakeNow);
			clockMicroseconds = 23;
		}
		{
			heroActionTiming::Scope bestTarget(&timing.fullBestTarget, fakeNow);
			clockMicroseconds = 36;
			throw std::runtime_error("best target");
		}
	}, std::runtime_error);
	EXPECT_EQ(timing.full.totalMicroseconds, 36u);
	EXPECT_EQ(timing.fullSetup.totalMicroseconds, 5u);
	EXPECT_EQ(timing.fullReachability.totalMicroseconds, 7u);
	EXPECT_EQ(timing.fullMovement.totalMicroseconds, 11u);
	EXPECT_EQ(timing.fullBestTarget.totalMicroseconds, 13u);
	EXPECT_EQ(timing.fullBestTarget.calls, 1u);
	EXPECT_EQ(timing.cached.calls, 0u);
}

TEST(HeroActionTimingTest, ParallelExceptionJoinsBeforePartialSlotReduction)
{
	heroActionTiming::Summary summary;
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 64);
	std::atomic<unsigned> finished{0};
	EXPECT_THROW({
		heroActionTiming::LoopScope loop(candidates, &summary);
		tbb::parallel_for(size_t{0}, size_t{64}, [&](size_t index)
		{
			struct Completion
			{
				std::atomic<unsigned> & finished;
				~Completion() { ++finished; }
			} completion{finished};
			heroActionTiming::Scope scope(&candidates.at(index)->full);
			if(index == 0)
				throw std::runtime_error("candidate");
		});
	}, std::runtime_error);
	EXPECT_TRUE(summary.loopJoined);
	EXPECT_TRUE(summary.loopThrew);
	EXPECT_GT(finished.load(), 0u);
	EXPECT_EQ(summary.continuation.full.calls, finished.load());
}

TEST(HeroActionTimingTest, LoopExceptionRetainsPartialSamplesAndMarksJoinedUnwinding)
{
	heroActionTiming::Summary summary;
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 2);
	clockMicroseconds = 50;
	EXPECT_THROW({
		heroActionTiming::LoopScope loop(candidates, &summary, fakeNow);
		candidates.at(0)->full = (heroActionTiming::Stage{1, 4, 4});
		candidates.at(0)->fullMovementDetails.findMoveExchangeDetails.body = (BattleMovementTiming::StageTiming{1, 3});
		clockMicroseconds = 61;
		throw std::runtime_error("candidate evaluation");
	}, std::runtime_error);
	EXPECT_TRUE(summary.loopJoined);
	EXPECT_TRUE(summary.loopThrew);
	EXPECT_EQ(summary.candidateLoop.totalMicroseconds, 11u);
	EXPECT_EQ(summary.continuation.full.calls, 1u);
	EXPECT_EQ(summary.continuation.fullMovementDetails.findMoveExchangeDetails.body.calls, 1u);
	EXPECT_EQ(summary.continuation.fullMovementDetails.findMoveExchangeDetails.body.totalMicroseconds, 3u);
}

TEST(HeroActionTimingTest, SubmitReturnAndThrowAreNotAuthoritativeCompletion)
{
	heroActionTiming::Summary summary;
	clockMicroseconds = 10;
	{
		heroActionTiming::SubmitScope submit(&summary, fakeNow);
		clockMicroseconds = 15;
	}
	clockMicroseconds = 20;
	EXPECT_THROW({
		heroActionTiming::SubmitScope submit(&summary, fakeNow);
		clockMicroseconds = 31;
		throw std::runtime_error("submit");
	}, std::runtime_error);
	EXPECT_EQ(summary.submitReturned, 1u);
	EXPECT_EQ(summary.submitThrew, 1u);
	EXPECT_EQ(summary.submit.calls, 2u);
	EXPECT_EQ(summary.submit.totalMicroseconds, 16u);
	EXPECT_EQ(summary.submit.maxMicroseconds, 11u);
}

TEST(HeroActionTimingTest, IndependentBudgetUsesExistingAsciiLimitContract)
{
	const char * malformedValues[]{nullptr, "", "0", "-1", "+1", " 1", "1x", "16385", "99999999999999999999"};
	for(const char * malformed : malformedValues)
		EXPECT_EQ(heroActionTiming::parseLimit(malformed), 1024u);
	EXPECT_EQ(heroActionTiming::parseLimit("1"), 1u);
	EXPECT_EQ(heroActionTiming::parseLimit("0012"), 12u);
	EXPECT_EQ(heroActionTiming::parseLimit("16384"), 16384u);
}

namespace
{
void fillCacheTiming(DamageCacheProbeTiming & cache, uint64_t scale)
{
	cache.body = {4 * scale, 0, 100 * scale};
	cache.preflight = {4 * scale, 0, 20 * scale};
	cache.dynamicDamage = {scale, 0, 70 * scale};
	cache.calculation.nativePayload = {2 * scale, 10 * scale};
	cache.calculation.bonusPreparation = {2 * scale, 20 * scale};
	cache.calculation.luaCall = {2 * scale, 30 * scale};
	cache.calculation.initialCalls = scale;
	cache.calculation.luckySecondCalls = scale;
	cache.calculation.unluckySecondCalls = 0;
	cache.calculation.suppressedStages = 3 * scale;
	cache.remainderNanoseconds = 10 * scale;
	cache.getDamageCalls = 3 * scale;
	cache.getOriginalDamageCalls = 2 * scale;
	cache.rootCacheHits = scale;
	cache.localChildCacheHits = scale;
	cache.parentOriginalHits = scale;
	cache.rootOriginalFallbacks = scale;
	cache.dynamicBypasses = scale;
	cache.suppressedReentrantCalls = 2 * scale;
	// Deliberately nonzero: operation-local bookkeeping must not be reduced.
	cache.operationsInFlight = 1;
	cache.dynamicCallbacksInFlight = 1;
	cache.phasesInFlight = 1;
}

void fillContinuationCacheTiming(heroActionTiming::Continuation & slot, uint64_t scale)
{
	fillCacheTiming(slot.fullMovementDetails.findMoveExchangeDetails.targetProbeDamageCache, scale);
	fillCacheTiming(slot.fullBestTargetDetails.waitedDetails.targetProbeDamageCache, 2 * scale);
	fillCacheTiming(slot.fullBestTargetDetails.normalDetails.targetProbeDamageCache, 3 * scale);
}

void expectCacheTiming(const heroActionTiming::Continuation & total, uint64_t scale)
{
	std::map<std::string, int64_t> fields;
	std::istringstream stream(heroActionTiming::damageCacheFields(total));
	std::string token;
	while(stream >> token)
	{
		const auto split = token.find('=');
		ASSERT_NE(split, std::string::npos);
		EXPECT_TRUE(fields.emplace(token.substr(0, split), std::stoll(token.substr(split + 1))).second);
	}
	ASSERT_EQ(fields.size(), 87u);
	for(const auto & [context, multiplier] : {
		std::pair{"movement", 1u}, std::pair{"best_target_waited", 2u}, std::pair{"best_target_normal", 3u}})
	{
		const std::string prefix = std::string("continuation_full_") + context + "_probe_cache_";
		const auto factor = scale * multiplier;
		EXPECT_EQ(fields.at(prefix + "body_calls"), 4 * factor);
		EXPECT_EQ(fields.at(prefix + "body_ns"), 100 * factor);
		EXPECT_EQ(fields.at(prefix + "preflight_calls"), 4 * factor);
		EXPECT_EQ(fields.at(prefix + "preflight_ns"), 20 * factor);
		EXPECT_EQ(fields.at(prefix + "dynamic_calls"), factor);
		EXPECT_EQ(fields.at(prefix + "dynamic_ns"), 70 * factor);
		EXPECT_EQ(fields.at(prefix + "native_payload_calls"), 2 * factor);
		EXPECT_EQ(fields.at(prefix + "native_payload_ns"), 10 * factor);
		EXPECT_EQ(fields.at(prefix + "bonus_preparation_calls"), 2 * factor);
		EXPECT_EQ(fields.at(prefix + "bonus_preparation_ns"), 20 * factor);
		EXPECT_EQ(fields.at(prefix + "lua_call_calls"), 2 * factor);
		EXPECT_EQ(fields.at(prefix + "lua_call_ns"), 30 * factor);
		EXPECT_EQ(fields.at(prefix + "initial_calculations"), factor);
		EXPECT_EQ(fields.at(prefix + "lucky_second_calculations"), factor);
		EXPECT_EQ(fields.at(prefix + "unlucky_second_calculations"), 0u);
		EXPECT_EQ(fields.at(prefix + "suppressed_calculation_stages"), 3 * factor);
		EXPECT_EQ(fields.at(prefix + "remainder_ns"), 10 * factor);
		EXPECT_EQ(fields.at(prefix + "body_ns"), fields.at(prefix + "preflight_ns")
			+ fields.at(prefix + "dynamic_ns") + fields.at(prefix + "remainder_ns"));
		for(const auto & [name, count] : {
			std::pair{"current_calls", 3u}, std::pair{"original_calls", 2u},
			std::pair{"root_hits", 1u}, std::pair{"child_hits", 1u},
			std::pair{"parent_original_hits", 1u}, std::pair{"root_original_fallbacks", 1u},
			std::pair{"parent_original_fallbacks", 0u}, std::pair{"dynamic_bypasses", 1u},
			std::pair{"fills", 0u}, std::pair{"dead_returns", 0u},
			std::pair{"failed_operations", 0u}, std::pair{"suppressed_reentries", 2u}})
			EXPECT_EQ(fields.at(prefix + name), count * factor);
		EXPECT_EQ(fields.at(prefix + "body_calls"), fields.at(prefix + "current_calls")
			+ fields.at(prefix + "original_calls") - fields.at(prefix + "root_original_fallbacks")
			- fields.at(prefix + "parent_original_fallbacks"));
		EXPECT_EQ(fields.at(prefix + "body_calls"), fields.at(prefix + "root_hits")
			+ fields.at(prefix + "child_hits") + fields.at(prefix + "parent_original_hits")
			+ fields.at(prefix + "dynamic_bypasses") + fields.at(prefix + "dead_returns")
			+ fields.at(prefix + "failed_operations"));
	}
}
}

TEST(HeroActionTimingTest, DamageCacheExtraOptInCannotAdmitDisabledCall)
{
	const char * disabledValues[]{nullptr, "", "0", "true", "01", "1 ", "1"};
	for(const char * value : disabledValues)
		EXPECT_FALSE(heroActionTiming::damageCacheTimingRequested(false, value));
	const char * invalidValues[]{nullptr, "", "0", "true", "01", "1 "};
	for(const char * value : invalidValues)
		EXPECT_FALSE(heroActionTiming::damageCacheTimingRequested(true, value));
	EXPECT_TRUE(heroActionTiming::damageCacheTimingRequested(true, "1"));
	heroActionTiming::Continuation slot;
	fillContinuationCacheTiming(slot, 1);
	EXPECT_TRUE(heroActionTiming::damageCacheFields(slot).empty());
	heroActionTiming::configureDamageCacheTiming(slot, true);
	EXPECT_TRUE(slot.fullMovementDetails.findMoveExchangeDetails.collectProbeDamageCache);
	EXPECT_TRUE(slot.fullBestTargetDetails.waitedDetails.collectProbeDamageCache);
	EXPECT_TRUE(slot.fullBestTargetDetails.normalDetails.collectProbeDamageCache);
	heroActionTiming::configureDamageCacheTiming(slot, false);
	EXPECT_TRUE(heroActionTiming::damageCacheFields(slot).empty());
}

TEST(HeroActionTimingTest, DamageCacheReductionJoinsContextsAndPreservesNestedClosure)
{
	heroActionTiming::Candidates candidates;
	candidates.prepare(true, 3);
	heroActionTiming::Summary summary;
	clockReads = 0;
	{
		heroActionTiming::LoopScope loop(candidates, &summary, fakeNow);
		for(size_t index = 0; index < 2; ++index)
		{
			auto & slot = *candidates.at(index);
			heroActionTiming::configureDamageCacheTiming(slot, true);
			fillContinuationCacheTiming(slot, index + 1);
		}
		// Unrequested slot data is not a measured sample and must be ignored.
		fillContinuationCacheTiming(*candidates.at(2), 100);
		EXPECT_TRUE(heroActionTiming::damageCacheFields(summary.continuation).empty());
	}
	EXPECT_EQ(clockReads, 2u); // Only existing loop wall reads, not reduction/formatting.
	ASSERT_TRUE(summary.loopJoined);
	EXPECT_FALSE(summary.loopThrew);
	expectCacheTiming(summary.continuation, 3);
	for(const auto * exchange : {&summary.continuation.fullMovementDetails.findMoveExchangeDetails,
		&summary.continuation.fullBestTargetDetails.waitedDetails,
		&summary.continuation.fullBestTargetDetails.normalDetails})
	{
		EXPECT_TRUE(exchange->collectProbeDamageCache);
		EXPECT_EQ(exchange->targetProbeDamageCache.operationsInFlight, 0u);
		EXPECT_EQ(exchange->targetProbeDamageCache.dynamicCallbacksInFlight, 0u);
		EXPECT_EQ(exchange->targetProbeDamageCache.phasesInFlight, 0u);
	}
}

namespace
{
bool exerciseCall()
{
	heroActionTiming::Call call;
	if(!call.enabled())
		return false;
	call.identify({17, 1, 1, 3, 7});
	call.prepare(2);
	{
		heroActionTiming::LoopScope loop(call.candidateSlots(), call.sink());
		heroActionTiming::Scope continuation(&call.continuation(0)->full);
		heroActionTiming::Scope setup(&call.continuation(0)->fullSetup);
		call.continuation(0)->fullBestTargetDetails.normalDetails.body = {2, 11};
		call.continuation(0)->fullBestTargetDetails.normalDetails.appliedAttacks = {3, 7};
		call.continuation(0)->fullBestTargetDetails.normalDetails.remainderMicroseconds = 4;
		call.continuation(0)->fullMovementDetails.findMoveExchange = {3, 19};
		call.continuation(0)->fullMovementDetails.findMoveExchangeDetails.body = {3, 17};
		call.continuation(0)->fullMovementDetails.findMoveExchangeDetails.appliedAttacks = {5, 13};
		call.continuation(0)->fullMovementDetails.findMoveExchangeDetails.remainderMicroseconds = 4;
	}
	{
		heroActionTiming::SubmitScope submit(call.sink());
	}
	return true;
}
}

TEST(HeroActionCallIntegrationTest, FreshProcessOptInAndCapOne)
{
	heroTimingTest::Capture capture;
	if(heroTimingTest::optedIn())
	{
		ASSERT_EQ(heroActionTiming::parseLimit(std::getenv("NH_RENDER_LOAD_TIMING_LIMIT")), 1u)
			<< "Run this exact filter alone in a fresh process with limit=1";
	}
	EXPECT_EQ(exerciseCall(), heroTimingTest::optedIn());
	EXPECT_FALSE(exerciseCall());
	EXPECT_FALSE(exerciseCall());
	const auto rows = capture.rows();
	if(!heroTimingTest::optedIn())
	{
		EXPECT_TRUE(rows.empty());
		return;
	}
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_TRUE(rows.back().starts_with("NH_HERO_ACTION_TIMING cap=1 reached;"));
	const auto row = heroTimingTest::fields(rows.front());
	EXPECT_EQ(row.at("hero_attempt"), 1);
	EXPECT_EQ(row.at("battle"), 17);
	EXPECT_EQ(row.at("player"), 1);
	EXPECT_EQ(row.at("side"), 1);
	EXPECT_EQ(row.at("stack"), 7);
	EXPECT_EQ(row.at("round"), 3);
	EXPECT_EQ(row.at("candidates"), 2);
	EXPECT_EQ(row.at("loop_joined"), 1);
	EXPECT_EQ(row.at("continuation_full_calls"), 1);
	EXPECT_EQ(row.at("continuation_cached_calls"), 0);
	EXPECT_EQ(row.at("continuation_full_setup_calls"), 1);
	EXPECT_EQ(row.at("continuation_full_reachability_calls"), 0);
	EXPECT_EQ(row.at("continuation_full_movement_calls"), 0);
	EXPECT_EQ(row.at("continuation_full_best_target_calls"), 0);
	EXPECT_EQ(row.at("continuation_full_best_target_normal_body_calls"), 2);
	EXPECT_EQ(row.at("continuation_full_best_target_normal_body_sum_us"), 11);
	EXPECT_EQ(row.at("continuation_full_best_target_normal_applied_attacks_calls"), 3);
	EXPECT_EQ(row.at("continuation_full_best_target_normal_remainder_us"), 4);
	EXPECT_EQ(row.at("continuation_full_best_target_waited_body_calls"), 0);
	EXPECT_EQ(row.at("continuation_full_movement_exchange_calls"), 3);
	EXPECT_EQ(row.at("continuation_full_movement_exchange_sum_us"), 19);
	EXPECT_EQ(row.at("continuation_full_movement_body_calls"), 3);
	EXPECT_EQ(row.at("continuation_full_movement_body_sum_us"), 17);
	EXPECT_EQ(row.at("continuation_full_movement_applied_attacks_calls"), 5);
	EXPECT_EQ(row.at("continuation_full_movement_remainder_us"), 4);
	EXPECT_EQ(row.at("submit_calls"), 1);
	EXPECT_EQ(row.at("submit_returned"), 1);
}

TEST(HeroActionDamageCacheCallIntegrationTest, FreshProcessOptInAndCapOne)
{
	heroTimingTest::Capture capture;
	ASSERT_EQ(heroActionTiming::parseLimit(std::getenv("NH_RENDER_LOAD_TIMING_LIMIT")), 1u)
		<< "Run this exact filter alone in a fresh process with limit=1, including OFF";
	const bool extra = heroActionTiming::damageCacheTimingRequested(heroTimingTest::optedIn(),
		std::getenv("NH_HERO_DAMAGE_CACHE_TIMING"));
	clockReads = 0;
	{
		heroActionTiming::Call call;
		EXPECT_EQ(call.enabled(), heroTimingTest::optedIn());
		call.identify({17, 1, 1, 3, 7});
		call.prepare(2);
		heroActionTiming::LoopScope loop(call.candidateSlots(), call.sink(), fakeNow);
		for(size_t index = 0; index < 2; ++index)
		{
			auto * slot = call.continuation(index);
			if(!heroTimingTest::optedIn())
			{
				EXPECT_EQ(slot, nullptr);
				continue;
			}
			ASSERT_NE(slot, nullptr);
			EXPECT_EQ(slot->fullMovementDetails.findMoveExchangeDetails.collectProbeDamageCache, extra);
			EXPECT_EQ(slot->fullBestTargetDetails.waitedDetails.collectProbeDamageCache, extra);
			EXPECT_EQ(slot->fullBestTargetDetails.normalDetails.collectProbeDamageCache, extra);
			if(extra)
				fillContinuationCacheTiming(*slot, index + 1);
		}
	}
	EXPECT_EQ(clockReads, heroTimingTest::optedIn() ? 2u : 0u);
	EXPECT_FALSE(exerciseCall());
	EXPECT_FALSE(exerciseCall());
	const auto rows = capture.rows();
	ASSERT_EQ(rows.size(), heroTimingTest::optedIn() ? 2u : 0u);
	if(!heroTimingTest::optedIn())
		return;
	EXPECT_TRUE(rows.back().starts_with("NH_HERO_ACTION_TIMING cap=1 reached;"));
	const auto actual = heroTimingTest::fields(rows.front());
	EXPECT_EQ(actual.at("hero_attempt"), 1);
	EXPECT_EQ(actual.at("loop_joined"), 1);
	EXPECT_EQ(actual.at("loop_threw"), 0);
	EXPECT_EQ(actual.at("candidates"), 2);
	EXPECT_EQ(actual.size(), extra ? 197u : 110u);
	if(extra)
	{
		heroActionTiming::Continuation expected;
		heroActionTiming::configureDamageCacheTiming(expected, true);
		fillContinuationCacheTiming(expected, 3);
		expectCacheTiming(expected, 3);
		std::istringstream stream(heroActionTiming::damageCacheFields(expected));
		std::string token;
		while(stream >> token)
		{
			const auto split = token.find('=');
			ASSERT_NE(split, std::string::npos);
			EXPECT_EQ(actual.at(token.substr(0, split)), std::stoll(token.substr(split + 1)));
		}
	}
}

namespace
{
void expectEmptyCacheContinuationCall(size_t count)
{
	heroTimingTest::Capture capture;
	ASSERT_EQ(heroActionTiming::parseLimit(std::getenv("NH_RENDER_LOAD_TIMING_LIMIT")), 1u)
		<< "Run this exact filter alone in a fresh process with limit=1, including OFF";
	const bool extra = heroActionTiming::damageCacheTimingRequested(heroTimingTest::optedIn(),
		std::getenv("NH_HERO_DAMAGE_CACHE_TIMING"));
	clockReads = 0;
	{
		heroActionTiming::Call call;
		EXPECT_EQ(call.enabled(), heroTimingTest::optedIn());
		call.prepare(count);
		heroActionTiming::LoopScope loop(call.candidateSlots(), call.sink(), fakeNow);
		// No continuation lookup or measured cache operation, including count=0.
	}
	EXPECT_EQ(clockReads, heroTimingTest::optedIn() ? 2u : 0u);
	EXPECT_FALSE(exerciseCall());
	const auto rows = capture.rows();
	ASSERT_EQ(rows.size(), heroTimingTest::optedIn() ? 2u : 0u);
	if(!heroTimingTest::optedIn())
		return;
	EXPECT_TRUE(rows.back().starts_with("NH_HERO_ACTION_TIMING cap=1 reached;"));
	const auto actual = heroTimingTest::fields(rows.front());
	EXPECT_EQ(actual.at("hero_attempt"), 1);
	EXPECT_EQ(actual.at("loop_joined"), 1);
	EXPECT_EQ(actual.at("loop_threw"), 0);
	EXPECT_EQ(actual.at("candidates"), count);
	EXPECT_EQ(actual.size(), extra ? 197u : 110u);
	size_t cacheFields = 0;
	for(const auto & [name, value] : actual)
	{
		if(name.find("_probe_cache_") != std::string::npos)
		{
			++cacheFields;
			EXPECT_EQ(value, 0);
		}
	}
	EXPECT_EQ(cacheFields, extra ? 87u : 0u);
}
}

TEST(HeroActionDamageCacheCallIntegrationTest, FreshProcessZeroSlotsRetainsSchema)
{
	expectEmptyCacheContinuationCall(0);
}

TEST(HeroActionDamageCacheCallIntegrationTest, FreshProcessNoContinuationRetainsSchema)
{
	expectEmptyCacheContinuationCall(2);
}

TEST(HeroActionCallIntegrationTest, GlobalLoggerGateDoesNotSpendBudget)
{
	heroTimingTest::Capture capture(ELogLevel::ERROR, ELogLevel::DEBUG);
	EXPECT_FALSE(exerciseCall());
	EXPECT_TRUE(capture.rows().empty());
	CLogger::getGlobalLogger()->setLevel(ELogLevel::DEBUG);
	EXPECT_EQ(exerciseCall(), heroTimingTest::optedIn());
	const auto rows = capture.records();
	ASSERT_EQ(rows.size(), heroTimingTest::optedIn() ? 1u : 0u);
	if(!rows.empty())
	{
		EXPECT_EQ(rows.front().at("hero_attempt"), 1);
	}
}

TEST(HeroActionCallIntegrationTest, AiLoggerGateDoesNotSpendBudget)
{
	heroTimingTest::Capture capture(ELogLevel::DEBUG, ELogLevel::ERROR);
	EXPECT_FALSE(exerciseCall());
	EXPECT_TRUE(capture.rows().empty());
	CLogger::getLogger(CLoggerDomain("ai"))->setLevel(ELogLevel::DEBUG);
	EXPECT_EQ(exerciseCall(), heroTimingTest::optedIn());
	const auto rows = capture.records();
	ASSERT_EQ(rows.size(), heroTimingTest::optedIn() ? 1u : 0u);
	if(!rows.empty())
	{
		EXPECT_EQ(rows.front().at("hero_attempt"), 1);
	}
}

namespace
{
// Separate prefix target: the existing 110-field aggregate capture is unchanged.
class TypeTimingTarget final : public ILogTarget
{
	std::weak_ptr<heroTimingTest::Messages> messages;
public:
	explicit TypeTimingTarget(std::weak_ptr<heroTimingTest::Messages> messages) : messages(std::move(messages)) {}
	void write(const LogRecord & record) override
	{
		if(!record.message.starts_with("NH_HERO_ACTION_TYPE_TIMING "))
			return;
		if(const auto target = messages.lock())
		{
			std::lock_guard lock(target->mutex);
			if(target->rows.size() < 4)
				target->rows.push_back(record.message);
			else
				target->truncated = true;
		}
	}
};

struct TypeTimingCapture
{
	heroTimingTest::Capture aggregate;
	std::shared_ptr<heroTimingTest::Messages> messages = std::make_shared<heroTimingTest::Messages>();
	explicit TypeTimingCapture(ELogLevel::ELogLevel global = ELogLevel::DEBUG,
		ELogLevel::ELogLevel ai = ELogLevel::DEBUG) : aggregate(global, ai)
	{
		CLogger::getGlobalLogger()->addTarget(std::make_unique<TypeTimingTarget>(messages));
	}
	std::vector<std::string> rows() const
	{
		std::lock_guard lock(messages->mutex);
		EXPECT_FALSE(messages->truncated);
		return messages->rows;
	}
};

bool exerciseTypedCall()
{
	heroActionTiming::Call call;
	if(!call.enabled())
		return false;
	call.identify({17, 1, 1, 3, 7});
	call.prepare(3);
	heroActionTiming::LoopScope loop(call.candidateSlots(), call.sink(), fakeNow);
	for(size_t index = 0; index < 3; ++index)
	{
		auto * slot = call.continuation(index);
		slot->command = index == 1 ? 0 : 10;
		slot->spell = index == 1 ? 7 : -1;
		heroActionTiming::Scope candidate(&slot->candidate, fakeNow);
		heroActionTiming::Scope continuation(index == 1 ? &slot->cached : &slot->full, fakeNow);
		clockMicroseconds += static_cast<int64_t>(index + 1) * 10;
		if(index != 1)
		{
			slot->fullMovement = {1, 3 * (index + 1), 3 * (index + 1)};
			slot->fullBestTarget = {1, index + 1, index + 1};
		}
	}
	return true;
}

void expectTypedRows(const TypeTimingCapture & capture)
{
	const auto rows = capture.rows();
	if(!heroTimingTest::optedIn())
	{
		EXPECT_TRUE(rows.empty());
		EXPECT_TRUE(capture.aggregate.rows().empty());
		return;
	}
	ASSERT_EQ(rows.size(), 2u);
	const auto parents = capture.aggregate.records();
	ASSERT_EQ(parents.size(), 1u);
	EXPECT_EQ(parents.front().size(), 110u);
	EXPECT_EQ(parents.front().at("hero_attempt"), 1);
	EXPECT_EQ(parents.front().at("candidates"), 3);
	uint64_t evaluations = 0, fullCalls = 0, cachedCalls = 0, fullSum = 0, cachedSum = 0;
	for(size_t index = 0; index < rows.size(); ++index)
	{
		std::map<std::string, int64_t> fields;
		std::istringstream stream(rows[index]);
		std::string token;
		stream >> token;
		EXPECT_EQ(token, "NH_HERO_ACTION_TYPE_TIMING");
		while(stream >> token)
		{
			const auto split = token.find('=');
			ASSERT_NE(split, std::string::npos);
			EXPECT_TRUE(fields.emplace(token.substr(0, split), std::stoll(token.substr(split + 1))).second);
		}
		ASSERT_EQ(fields.size(), 20u);
		for(const auto * key : {"battle", "player", "side", "stack", "round", "hero_attempt", "loop_joined", "loop_threw"})
			EXPECT_EQ(fields.at(key), parents.front().at(key));
		EXPECT_EQ(fields.at("loop_joined"), 1);
		EXPECT_EQ(fields.at("loop_threw"), 0);
		EXPECT_EQ(fields.at("command"), index == 0 ? 10 : 0);
		EXPECT_EQ(fields.at("spell"), index == 0 ? -1 : 7);
		EXPECT_EQ(fields.at("overflow"), 0);
		EXPECT_EQ(fields.at("evaluations"), index == 0 ? 2 : 1);
		EXPECT_EQ(fields.at("sum_us"), index == 0 ? 40 : 20);
		EXPECT_EQ(fields.at("max_us"), index == 0 ? 30 : 20);
		EXPECT_EQ(fields.at("full_calls"), index == 0 ? 2 : 0);
		EXPECT_EQ(fields.at("full_sum_us"), index == 0 ? 40 : 0);
		EXPECT_EQ(fields.at("cached_calls"), index == 0 ? 0 : 1);
		EXPECT_EQ(fields.at("cached_sum_us"), index == 0 ? 0 : 20);
		EXPECT_EQ(fields.at("movement_sum_us"), index == 0 ? 12 : 0);
		EXPECT_EQ(fields.at("best_target_sum_us"), index == 0 ? 4 : 0);
		evaluations += fields.at("evaluations");
		fullCalls += fields.at("full_calls");
		cachedCalls += fields.at("cached_calls");
		fullSum += fields.at("full_sum_us");
		cachedSum += fields.at("cached_sum_us");
	}
	EXPECT_EQ(evaluations, parents.front().at("candidates"));
	EXPECT_EQ(fullCalls, parents.front().at("continuation_full_calls"));
	EXPECT_EQ(cachedCalls, parents.front().at("continuation_cached_calls"));
	EXPECT_EQ(fullSum, parents.front().at("continuation_full_sum_us"));
	EXPECT_EQ(cachedSum, parents.front().at("continuation_cached_sum_us"));
}
}

TEST(HeroActionTypeCallIntegrationTest, FreshProcessScopedTypesAndCapOne)
{
	TypeTimingCapture capture;
	ASSERT_EQ(heroActionTiming::parseLimit(std::getenv("NH_RENDER_LOAD_TIMING_LIMIT")), 1u)
		<< "Run this exact filter alone in a fresh process with limit=1, including OFF";
	clockReads = 0;
	clockMicroseconds = 100;
	EXPECT_EQ(exerciseTypedCall(), heroTimingTest::optedIn());
	EXPECT_FALSE(exerciseTypedCall());
	EXPECT_FALSE(exerciseTypedCall());
	expectTypedRows(capture);
	EXPECT_EQ(clockReads, heroTimingTest::optedIn() ? 14u : 0u);
	if(heroTimingTest::optedIn())
	{
		const auto rows = capture.aggregate.rows();
		ASSERT_EQ(rows.size(), 2u);
		EXPECT_TRUE(rows.back().starts_with("NH_HERO_ACTION_TIMING cap=1 reached;"));
	}
}

TEST(HeroActionTypeCallIntegrationTest, GlobalLoggerGateDoesNotSpendTypeBudget)
{
	TypeTimingCapture capture(ELogLevel::ERROR, ELogLevel::DEBUG);
	clockReads = 0;
	EXPECT_FALSE(exerciseTypedCall());
	EXPECT_TRUE(capture.rows().empty());
	EXPECT_TRUE(capture.aggregate.rows().empty());
	EXPECT_EQ(clockReads, 0u);
	CLogger::getGlobalLogger()->setLevel(ELogLevel::DEBUG);
	EXPECT_EQ(exerciseTypedCall(), heroTimingTest::optedIn());
	expectTypedRows(capture);
}

TEST(HeroActionTypeCallIntegrationTest, AiLoggerGateDoesNotSpendTypeBudget)
{
	TypeTimingCapture capture(ELogLevel::DEBUG, ELogLevel::ERROR);
	clockReads = 0;
	EXPECT_FALSE(exerciseTypedCall());
	EXPECT_TRUE(capture.rows().empty());
	EXPECT_TRUE(capture.aggregate.rows().empty());
	EXPECT_EQ(clockReads, 0u);
	CLogger::getLogger(CLoggerDomain("ai"))->setLevel(ELogLevel::DEBUG);
	EXPECT_EQ(exerciseTypedCall(), heroTimingTest::optedIn());
	expectTypedRows(capture);
}
