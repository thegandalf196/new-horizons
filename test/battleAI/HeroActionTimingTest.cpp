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
		heroActionTiming::LoopScope loop(candidates, nullptr, fakeNow);
		heroActionTiming::SubmitScope submit(nullptr, fakeNow);
	}
	EXPECT_EQ(clockReads, 0u);
	EXPECT_EQ(candidates.size(), 0u);
	EXPECT_EQ(candidates.at(0), nullptr);
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
