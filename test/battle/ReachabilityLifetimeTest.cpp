/*
 * ReachabilityLifetimeTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <optional>

namespace
{
template<typename T> std::vector<std::byte> wire(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

class ReachabilityLifetimeTest : public HeroCommandFixture
{};

TEST_F(ReachabilityLifetimeTest, ReturnedWalkingCopyAndMoveOwnCallerCostSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	const auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 10);
	const auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 5), 10);
	const auto world = gameState()->saveToMemory();
	const auto liveBattle = wire(*battle());
	const auto serverRng = wire(*gameHandler->randomizer);
	const auto gameRng = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto aiRng = wire(CRandomGenerator::getDefault());
	std::optional<ReachabilityInfo> retained;
	TBattlefieldTurnsArray costs;
	ReachabilityInfo::TDistances distances;
	ReachabilityInfo::TPredecessors predecessors;
	TAccessibilityArray accessibility;
	{
		ReachabilityInfo::Parameters params(actor, actor->getPosition());
		params.bypassEnemyStacks = true;
		params.destructibleEnemyTurns[enemy->getPosition().toInt()] = 7;
		params.destructibleEnemyTurns[BattleHex(8, 5).toInt()] = 11;
		costs = params.destructibleEnemyTurns;
		auto result = battle()->getReachability(params);
		ASSERT_TRUE(result.accessibility.destructibleEnemyTurns);
		// Deterministic RED before any caller storage is mutated or destroyed.
		ASSERT_NE(result.accessibility.destructibleEnemyTurns.get(), &params.destructibleEnemyTurns);
		EXPECT_EQ(*result.accessibility.destructibleEnemyTurns, costs);
		distances = result.distances;
		predecessors = result.predecessors;
		accessibility = result.accessibility;
		params.destructibleEnemyTurns.fill(-1);
		EXPECT_EQ(*result.accessibility.destructibleEnemyTurns, costs);
		EXPECT_EQ(result.params.destructibleEnemyTurns, costs);
		auto copied = result;
		auto moved = std::move(copied);
		retained.emplace(std::move(moved));
	}
	// A later query reuses local Parameters storage but cannot replace old costs.
	{
		ReachabilityInfo::Parameters replacement(actor, actor->getPosition());
		replacement.destructibleEnemyTurns.fill(3);
		const auto fresh = battle()->getReachability(replacement);
		ASSERT_TRUE(fresh.accessibility.destructibleEnemyTurns);
		EXPECT_NE(*fresh.accessibility.destructibleEnemyTurns, costs);
	}
	ASSERT_TRUE(retained);
	ASSERT_TRUE(retained->accessibility.destructibleEnemyTurns);
	EXPECT_EQ(*retained->accessibility.destructibleEnemyTurns, costs);
	EXPECT_EQ(retained->params.destructibleEnemyTurns, costs);
	EXPECT_EQ(retained->distances, distances);
	EXPECT_EQ(retained->predecessors, predecessors);
	EXPECT_EQ(static_cast<const TAccessibilityArray &>(retained->accessibility), accessibility);
	EXPECT_TRUE(retained->accessibility.accessible(enemy->getPosition(), false, BattleSide::ATTACKER));
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(wire(*battle()), liveBattle);
	EXPECT_EQ(wire(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(wire(CRandomGenerator::getDefault()), aiRng);
}
}
