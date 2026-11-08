/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include "../../AI/BattleAI/MovementQueryCache.h"
#include "../battle/MovementLegacyReference.h"
#include <gtest/gtest.h>
#include <stdexcept>
#include <utility>

namespace
{
struct World
{
	AccessibilityInfo accessibility;
	ReachabilityInfo::Parameters params;
	BattleHexArray stoppers;
	std::array<std::int32_t, GameConstants::BFIELD_SIZE> costs{};
	World()
	{
		params.startPosition = BattleHex(93);
		params.side = BattleSide::ATTACKER;
		params.knownAccessible = &battle::Unit::getHexes(params.startPosition, false, params.side);
		accessibility.fill(EAccessibility::ACCESSIBLE);
		for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
			if(!BattleHex(hex).isAvailable()) accessibility[hex] = EAccessibility::SIDE_COLUMN;
	}
	void compare(MovementQueryCache & cache) const
	{
		const auto expected = movementLegacy::solve(accessibility, params, stoppers, EGateState::CLOSED, costs);
		const auto actual = cache.getWalking(accessibility, params, stoppers, EGateState::CLOSED, costs);
		EXPECT_EQ(actual, expected);
	}
};
}

TEST(MovementQueryAdmissionTest, ReusedWalkingStillReusesAfterInitialDifferentActors)
{
	MovementQueryCache cache;
	World world;
	for(int candidate = 0; candidate < 8; ++candidate)
		for(int actor = 0; actor < 14; ++actor)
		{
			world.params.startPosition = BattleHex(40 + actor);
			world.params.knownAccessible = &battle::Unit::getHexes(world.params.startPosition, false, world.params.side);
			world.compare(cache);
		}
	EXPECT_EQ(cache.statistics().solves, 14u);
	EXPECT_EQ(cache.statistics().hits, 98u);
}

TEST(MovementQueryAdmissionTest, UniqueQueriesBoundMemoWorkAndNeverApproximate)
{
	MovementQueryCache cache;
	World world;
	for(int layout = 0; layout < 112; ++layout)
	{
		world.costs[94] = layout;
		world.compare(cache);
	}
	const auto stats = cache.statistics();
	EXPECT_EQ(stats.queries, 112u);
	EXPECT_EQ(stats.solves, 112u);
	EXPECT_EQ(stats.hits, 0u);
	EXPECT_GT(stats.bypassed, 0u);
	EXPECT_EQ(stats.entries, 1u); // The first repeated footprint misses, so stop publication.
	EXPECT_EQ(stats.bypassed, 110u);
	// Disabling admission does not prevent later state changes from being solved.
	world.params.ghostWalk = true;
	world.accessibility[94] = EAccessibility::ALIVE_STACK;
	world.compare(cache);
}

TEST(MovementQueryAdmissionTest, CheapAndDisabledQueriesKeepExactNativePaths)
{
	MovementQueryCache cache;
	MovementQueryCache disabled(0);
	World world;
	world.params.flying = true;
	world.compare(cache);
	world.compare(disabled);
	world.params.flying = false;
	world.accessibility.fill(EAccessibility::OBSTACLE);
	world.accessibility[93] = EAccessibility::ACCESSIBLE;
	world.accessibility[94] = EAccessibility::ACCESSIBLE;
	world.compare(cache);
	world.compare(disabled);
	EXPECT_EQ(cache.statistics().bypassed, 2u);
	EXPECT_EQ(cache.statistics().entries, 0u);
	EXPECT_EQ(disabled.statistics().solves, 2u);
}

TEST(MovementQueryAdmissionTest, DifferentFootprintsAtOneAnchorRemainEligibleForReuse)
{
	MovementQueryCache cache;
	World world;
	world.compare(cache);
	world.params.doubleWide = true;
	world.params.knownAccessible = &battle::Unit::getHexes(world.params.startPosition, true, world.params.side);
	world.compare(cache);
	world.params.side = BattleSide::DEFENDER;
	world.params.knownAccessible = &battle::Unit::getHexes(world.params.startPosition, true, world.params.side);
	world.compare(cache);
	world.params.doubleWide = false;
	world.params.side = BattleSide::ATTACKER;
	world.params.knownAccessible = &battle::Unit::getHexes(world.params.startPosition, false, world.params.side);
	world.compare(cache);
	EXPECT_EQ(cache.statistics().entries, 3u);
	EXPECT_EQ(cache.statistics().hits, 1u);
	EXPECT_EQ(cache.statistics().bypassed, 0u);
}

TEST(MovementQueryAdmissionTest, AHitKeepsLaterChangedInputsEligible)
{
	MovementQueryCache cache;
	World world;
	world.compare(cache);
	world.compare(cache);
	world.costs[94] = 3;
	world.compare(cache);
	world.compare(cache);
	EXPECT_EQ(cache.statistics().solves, 2u);
	EXPECT_EQ(cache.statistics().hits, 2u);
	EXPECT_EQ(cache.statistics().entries, 2u);
	EXPECT_EQ(cache.statistics().bypassed, 0u);
}

TEST(MovementQueryAdmissionTest, DistinctStartingFootprintsRetainTheHardMissCeiling)
{
	MovementQueryCache cache;
	World world;
	for(int start = 30; start < 63; ++start)
	{
		world.params.startPosition = BattleHex(start);
		world.params.knownAccessible = &battle::Unit::getHexes(world.params.startPosition, false, world.params.side);
		world.compare(cache);
	}
	EXPECT_EQ(cache.statistics().entries, 32u);
	EXPECT_EQ(cache.statistics().solves, 33u);
	EXPECT_EQ(cache.statistics().hits, 0u);
	EXPECT_EQ(cache.statistics().bypassed, 1u);
}

TEST(MovementQueryAdmissionTest, InitiallyCheapFieldKeepsExactPathsAfterItExpands)
{
	MovementQueryCache cache;
	World world;
	world.accessibility.fill(EAccessibility::OBSTACLE);
	world.accessibility[93] = EAccessibility::ACCESSIBLE;
	world.accessibility[94] = EAccessibility::ACCESSIBLE;
	world.compare(cache);
	world = World();
	world.compare(cache);
	world.compare(cache);
	EXPECT_EQ(cache.statistics().entries, 0u);
	EXPECT_EQ(cache.statistics().bypassed, 3u);
	EXPECT_EQ(cache.statistics().solves, 3u);
}

TEST(MovementQueryAdmissionTest, CheapInterludeDoesNotCloseAnAlreadyPopulatedMemo)
{
	MovementQueryCache cache;
	World world;
	world.compare(cache);
	world.accessibility.fill(EAccessibility::OBSTACLE);
	world.accessibility[93] = EAccessibility::ACCESSIBLE;
	world.compare(cache);
	world = World();
	world.compare(cache);
	EXPECT_EQ(cache.statistics().entries, 1u);
	EXPECT_EQ(cache.statistics().hits, 1u);
	EXPECT_EQ(cache.statistics().bypassed, 1u);
}

TEST(MovementQueryAdmissionTest, FailedFirstSolveDoesNotRecordAStartingFootprint)
{
	bool fail = true;
	MovementQueryCache cache(256, [&](const battle::MovementQuery & query)
	{
		if(std::exchange(fail, false)) throw std::runtime_error("Expected failed solve");
		return battle::solveMovementQuery(query);
	});
	World world;
	EXPECT_THROW(cache.getWalking(world.accessibility, world.params, world.stoppers, EGateState::CLOSED, world.costs), std::runtime_error);
	world.compare(cache);
	world.compare(cache);
	EXPECT_EQ(cache.statistics().solves, 1u);
	EXPECT_EQ(cache.statistics().hits, 1u);
	EXPECT_EQ(cache.statistics().entries, 1u);
	EXPECT_EQ(cache.statistics().bypassed, 0u);
}

TEST(MovementQueryAdmissionTest, FailedRepeatedMissDoesNotCloseAdmission)
{
	bool fail = true;
	MovementQueryCache cache(256, [&](const battle::MovementQuery & query)
	{
		if(query.movementCost[94] == 1 && std::exchange(fail, false))
			throw std::runtime_error("Expected failed repeated miss");
		return battle::solveMovementQuery(query);
	});
	World world;
	world.compare(cache);
	world.costs[94] = 1;
	EXPECT_THROW(cache.getWalking(world.accessibility, world.params, world.stoppers, EGateState::CLOSED, world.costs), std::runtime_error);
	world.costs[94] = 0;
	world.compare(cache);
	world.costs[94] = 1;
	world.compare(cache);
	world.compare(cache);
	EXPECT_EQ(cache.statistics().solves, 2u);
	EXPECT_EQ(cache.statistics().hits, 2u);
	EXPECT_EQ(cache.statistics().entries, 2u);
	EXPECT_EQ(cache.statistics().bypassed, 0u);
}
