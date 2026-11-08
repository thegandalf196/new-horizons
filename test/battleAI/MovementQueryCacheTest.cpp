/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include <gtest/gtest.h>
#include "../../AI/BattleAI/MovementQueryCache.h"
#include <future>
#include <stdexcept>
#include <utility>

namespace
{
battle::MovementQuery openQuery()
{
	battle::MovementQuery query;
	query.startPosition = 93;
	query.traversable.set();
	return query;
}

void expectSamePaths(const battle::MovementPaths & actual, const battle::MovementPaths & expected)
{
	EXPECT_EQ(actual.distances, expected.distances);
	EXPECT_EQ(actual.predecessors, expected.predecessors);
}
}

TEST(MovementQueryCacheTest, EqualOwnedInputsReuseAndEveryDependencyDistinguishesKeys)
{
	MovementQueryCache memo;
	const auto original = openQuery();
	expectSamePaths(memo.get(original), battle::solveMovementQuery(original));
	const auto copy = original;
	expectSamePaths(memo.get(copy), battle::solveMovementQuery(copy));
	std::vector<battle::MovementQuery> changes(7, original);
	changes[0].startPosition = 94;
	changes[1].footprint = 1;
	changes[2].flying = true;
	changes[3].traversable.reset(94);
	changes[4].stoppers.set(94);
	changes[5].movementCost[94] = 3;
	changes[6].bypassCost[94] = 4;
	for(const auto & changed : changes)
		expectSamePaths(memo.get(changed), battle::solveMovementQuery(changed));
	expectSamePaths(memo.get(original), battle::solveMovementQuery(original));
	const auto counts = memo.statistics();
	EXPECT_EQ(counts.queries, 10u);
	EXPECT_EQ(counts.hits, 2u);
	EXPECT_EQ(counts.solves, 8u);
	EXPECT_EQ(counts.entries, 8u);
}

TEST(MovementQueryCacheTest, CapacityBoundsStorageAndEvictedQueriesRecomputeIdentically)
{
	MovementQueryCache memo(2);
	const auto first = openQuery();
	auto second = first;
	second.startPosition = 94;
	auto third = first;
	third.startPosition = 95;
	for(const auto & query : {first, second, third, second, first})
		expectSamePaths(memo.get(query), battle::solveMovementQuery(query));
	const auto counts = memo.statistics();
	EXPECT_EQ(counts.entries, 2u);
	EXPECT_EQ(counts.solves, 4u);
	EXPECT_EQ(counts.hits, 1u);
	EXPECT_EQ(counts.evictions, 2u);
}

TEST(MovementQueryCacheTest, ZeroCapacityPreservesEverySolve)
{
	MovementQueryCache memo(0);
	const auto query = openQuery();
	for(int repeat = 0; repeat < 3; ++repeat)
		expectSamePaths(memo.get(query), battle::solveMovementQuery(query));
	const auto counts = memo.statistics();
	EXPECT_EQ(counts.solves, 3u);
	EXPECT_EQ(counts.hits, 0u);
	EXPECT_EQ(counts.entries, 0u);
	EXPECT_EQ(counts.evictions, 0u);
}

TEST(MovementQueryCacheTest, FailedSolveNeitherPublishesNorEvictsAndRetrySucceeds)
{
	bool fail = true;
	MovementQueryCache memo(1, [&](const battle::MovementQuery & query)
	{
		if(query.startPosition == 94 && std::exchange(fail, false))
			throw std::runtime_error("Expected movement solver failure");
		return battle::solveMovementQuery(query);
	});
	const auto first = openQuery();
	auto second = first;
	second.startPosition = 94;
	memo.get(first);
	EXPECT_THROW(memo.get(second), std::runtime_error);
	expectSamePaths(memo.get(first), battle::solveMovementQuery(first));
	EXPECT_EQ(memo.statistics().solves, 1u);
	EXPECT_EQ(memo.statistics().evictions, 0u);
	expectSamePaths(memo.get(second), battle::solveMovementQuery(second));
	EXPECT_EQ(memo.statistics().solves, 2u);
	EXPECT_EQ(memo.statistics().entries, 1u);
	EXPECT_EQ(memo.statistics().evictions, 1u);
}

TEST(MovementQueryCacheTest, ConcurrentBranchesPublishOneCompleteResult)
{
	MovementQueryCache memo;
	const auto query = openQuery();
	std::vector<std::future<battle::MovementPaths>> work;
	for(int index = 0; index < 8; ++index)
		work.push_back(std::async(std::launch::async, [&] { return memo.get(query); }));
	for(auto & result : work)
		expectSamePaths(result.get(), battle::solveMovementQuery(query));
	EXPECT_EQ(memo.statistics().solves, 1u);
	EXPECT_EQ(memo.statistics().hits, 7u);
}
