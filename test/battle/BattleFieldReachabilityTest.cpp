/*
 * BattleFieldReachabilityTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../client/battle/BattleFieldReachability.h"
#include "../../lib/CStack.h"
#include "../server/battles/HeroCommandFixture.h"
#include <stdexcept>

TEST(BattleFieldReachabilityTest, ActorSwitchRefreshesBeforePaintAndReusesForBothConsumers)
{
	BattleFieldReachability cache;
	CStack actorA;
	CStack actorB;
	unsigned queries = 0;
	EXPECT_TRUE(cache.get(&actorA, [&] { ++queries; return BattleHexArray{}; }).empty());
	const BattleHex target(8, 5);
	const auto & movement = cache.get(&actorB, [&] { ++queries; return BattleHexArray{target}; });
	EXPECT_TRUE(movement.contains(target));
	const auto & direction = cache.get(&actorB, [&] { ++queries; return BattleHexArray{}; });
	EXPECT_EQ(&movement, &direction);
	EXPECT_TRUE(direction.contains(target));
	EXPECT_EQ(queries, 2u); // No draw required, and the next draw can reuse it.
}

TEST(BattleFieldReachabilityTest, SameActorInvalidationRefreshesOnlyOnRead)
{
	BattleFieldReachability cache;
	CStack actor;
	unsigned queries = 0;
	const BattleHex oldHex(4, 5);
	const BattleHex newHex(5, 5);
	EXPECT_TRUE(cache.get(&actor, [&] { ++queries; return BattleHexArray{oldHex}; }).contains(oldHex));
	cache.invalidate();
	EXPECT_EQ(queries, 1u);
	const auto & fresh = cache.get(&actor, [&] { ++queries; return BattleHexArray{newHex}; });
	EXPECT_FALSE(fresh.contains(oldHex));
	EXPECT_TRUE(fresh.contains(newHex));
	EXPECT_TRUE(cache.get(&actor, [&] { ++queries; return BattleHexArray{}; }).contains(newHex));
	EXPECT_EQ(queries, 2u);
}

TEST(BattleFieldReachabilityTest, AbsentActorClearsWithoutQueryAndReturningActorRefreshes)
{
	BattleFieldReachability cache;
	CStack actor;
	unsigned queries = 0;
	const auto load = [&] { ++queries; return BattleHexArray{BattleHex(5, 5)}; };
	EXPECT_TRUE(cache.get(nullptr, load).empty());
	EXPECT_EQ(queries, 0u);
	EXPECT_FALSE(cache.get(&actor, load).empty());
	cache.invalidate();
	EXPECT_TRUE(cache.get(nullptr, load).empty());
	EXPECT_TRUE(cache.get(nullptr, load).empty());
	EXPECT_EQ(queries, 1u);
	EXPECT_FALSE(cache.get(&actor, load).empty());
	EXPECT_EQ(queries, 2u);
}

TEST(BattleFieldReachabilityTest, FailedRefreshRemainsDirtyUntilSuccessfulRetry)
{
	BattleFieldReachability cache;
	CStack actor;
	unsigned queries = 0;
	cache.get(&actor, [] { return BattleHexArray{BattleHex(4, 5)}; });
	cache.invalidate();
	EXPECT_THROW(cache.get(&actor, [&]() -> BattleHexArray
	{
		++queries;
		throw std::runtime_error("Expected reachability query failure");
	}), std::runtime_error);
	const BattleHex target(5, 5);
	EXPECT_TRUE(cache.get(&actor, [&] { ++queries; return BattleHexArray{target}; }).contains(target));
	EXPECT_TRUE(cache.get(&actor, [&] { ++queries; return BattleHexArray{}; }).contains(target));
	EXPECT_EQ(queries, 2u);
}

class BattleFieldReachabilityGeometryTest : public HeroCommandFixture
{};

TEST_F(BattleFieldReachabilityGeometryTest, ActiveSwitchMatchesFreshMovementAndAttackGeometryWithoutDraw)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	auto * actorA = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 2), 10);
	auto * actorB = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(11, 5), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
	ASSERT_NE(actorA, nullptr);
	ASSERT_NE(actorB, nullptr);
	ASSERT_NE(target, nullptr);
	BattleFieldReachability cache;
	unsigned queries = 0;
	const auto stale = cache.get(actorA, [&]
	{
		++queries;
		return battle()->battleGetAvailableHexes(actorA, false);
	});
	ASSERT_FALSE(battle()->battleCanAttackHex(stale, actorB, target->getPosition()));
	const auto expected = battle()->battleGetAvailableHexes(actorB, false);
	ASSERT_TRUE(battle()->battleCanAttackHex(expected, actorB, target->getPosition()));
	const auto load = [&] { ++queries; return battle()->battleGetAvailableHexes(actorB, false); };
	const auto & movement = cache.get(actorB, load);
	EXPECT_EQ(movement, expected);
	const BattleHex destination(10, 5);
	ASSERT_TRUE(battle()->toWhichHexMove(expected, actorB, destination).isValid());
	EXPECT_EQ(battle()->toWhichHexMove(movement, actorB, destination),
		battle()->toWhichHexMove(expected, actorB, destination));
	const auto & direction = cache.get(actorB, load);
	for(int value = 0; value < 8; ++value)
	{
		const auto approach = BattleHex::EDir(value);
		EXPECT_EQ(battle()->battleCanAttackHex(direction, actorB, target->getPosition(), approach),
			battle()->battleCanAttackHex(expected, actorB, target->getPosition(), approach));
	}
	EXPECT_EQ(queries, 2u);
}
