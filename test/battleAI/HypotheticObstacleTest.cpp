/*
 * HypotheticObstacleTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CPlayerBattleCallback.h"

namespace
{
class ObstacleEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ObstacleEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class HypotheticObstacleTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<ObstacleEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	void prepareObstacles(bool opening = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		if(!opening)
			ASSERT_NO_FATAL_FAILURE(beginCombat());
		environment = std::make_shared<ObstacleEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}

	SpellCreatedObstacle wall(int id, int turns)
	{
		SpellCreatedObstacle obstacle;
		obstacle.uniqueID = id;
		obstacle.ID = SpellID::FORCE_FIELD;
		obstacle.trigger = SpellID::NONE;
		obstacle.pos = BattleHex(8, 5);
		obstacle.customSize.insert(obstacle.pos);
		obstacle.turnsRemaining = turns;
		obstacle.casterSide = BattleSide::ATTACKER;
		obstacle.nativeVisible = false;
		return obstacle;
	}

	ObstacleChanges describe(SpellCreatedObstacle obstacle,
		BattleChanges::EOperation operation = BattleChanges::EOperation::ADD)
	{
		ObstacleChanges changes;
		obstacle.toInfo(changes, operation);
		return changes;
	}

	void addLive(const SpellCreatedObstacle & obstacle)
	{
		BattleObstaclesChanged pack;
		pack.battleID = BattleID(0);
		pack.change = describe(obstacle);
		gameHandler->sendAndApply(pack);
	}
};

TEST_F(HypotheticObstacleTest, AddUpdateRemoveAffectModelAccessibilityNotLiveBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareObstacles());
	HypotheticBattle model(environment.get(), callback);
	const auto obstacle = wall(7, 2);
	ASSERT_EQ(battle()->getAccessibility()[obstacle.pos.toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_FALSE(model.hasObstacleChanges());
	model.removeObstacle(999);
	EXPECT_FALSE(model.hasObstacleChanges());
	model.addObstacle(describe(obstacle));
	EXPECT_TRUE(model.hasObstacleChanges());
	EXPECT_EQ(model.getAccessibility()[obstacle.pos.toInt()], EAccessibility::OBSTACLE);
	EXPECT_EQ(battle()->getAccessibility()[obstacle.pos.toInt()], EAccessibility::ACCESSIBLE);

	auto update = obstacle;
	update.revealed = true;
	update.turnsRemaining = 99;
	update.pos = BattleHex(9, 5);
	update.customSize.clear();
	update.customSize.insert(BattleHex(9, 5));
	model.updateObstacle(describe(update, BattleChanges::EOperation::UPDATE));
	ASSERT_EQ(model.getAllObstacles().size(), 1u);
	const auto changed = std::dynamic_pointer_cast<const SpellCreatedObstacle>(model.getAllObstacles().front());
	ASSERT_TRUE(changed);
	EXPECT_TRUE(changed->revealed);
	EXPECT_EQ(changed->turnsRemaining, 2);
	EXPECT_EQ(changed->pos, obstacle.pos);
	EXPECT_TRUE(changed->getBlockedTiles().contains(obstacle.pos));
	model.removeObstacle(7);
	EXPECT_TRUE(model.getAllObstacles().empty());
	EXPECT_EQ(model.getAccessibility()[obstacle.pos.toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_TRUE(battle()->getAllObstacles().empty());
}

TEST_F(HypotheticObstacleTest, NestedRevealAndAgingKeepParentLiveAndRetainedViewsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepareObstacles());
	const auto obstacle = wall(7, 2);
	addLive(obstacle);
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	EXPECT_FALSE(child.hasObstacleChanges());
	const auto retained = std::dynamic_pointer_cast<const SpellCreatedObstacle>(child.getAllObstacles().front());
	auto update = obstacle;
	update.revealed = true;
	child.updateObstacle(describe(update, BattleChanges::EOperation::UPDATE));
	EXPECT_TRUE(child.hasObstacleChanges());
	EXPECT_FALSE(parent->hasObstacleChanges());
	child.nextRound();
	const auto current = std::dynamic_pointer_cast<const SpellCreatedObstacle>(child.getAllObstacles().front());
	EXPECT_TRUE(current->revealed);
	EXPECT_EQ(current->turnsRemaining, 1);
	EXPECT_FALSE(retained->revealed);
	EXPECT_EQ(retained->turnsRemaining, 2);
	for(const auto & view : {parent->getAllObstacles(), battle()->getAllObstacles()})
	{
		const auto unchanged = std::dynamic_pointer_cast<const SpellCreatedObstacle>(view.front());
		EXPECT_FALSE(unchanged->revealed);
		EXPECT_EQ(unchanged->turnsRemaining, 2);
	}
	HypotheticBattle removal(environment.get(), parent);
	EXPECT_FALSE(removal.hasObstacleChanges());
	removal.removeObstacle(7);
	EXPECT_TRUE(removal.hasObstacleChanges());
	EXPECT_EQ(removal.getAccessibility()[obstacle.pos.toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_EQ(parent->getAccessibility()[obstacle.pos.toInt()], EAccessibility::OBSTACLE);
	EXPECT_EQ(battle()->getAccessibility()[obstacle.pos.toInt()], EAccessibility::OBSTACLE);
}

TEST_F(HypotheticObstacleTest, ObstacleExpiryTicksEvenOnOpeningRoundAndKeepsInfiniteDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepareObstacles(true));
	ASSERT_EQ(battle()->battleGetRound(), 0);
	addLive(wall(7, 1));
	addLive(wall(8, -1));
	addLive(wall(9, 0));
	HypotheticBattle model(environment.get(), callback);
	EXPECT_FALSE(model.hasObstacleChanges());
	model.nextRound();
	const auto remaining = model.getAllObstacles();
	ASSERT_EQ(remaining.size(), 1u);
	EXPECT_EQ(remaining.front()->uniqueID, 8);
	EXPECT_EQ(std::dynamic_pointer_cast<const SpellCreatedObstacle>(remaining.front())->turnsRemaining, -1);
	EXPECT_TRUE(model.hasObstacleChanges());
	EXPECT_EQ(battle()->getAllObstacles().size(), 3u);
}

TEST_F(HypotheticObstacleTest, SnapshotDoesNotDiscoverEnemyHiddenObstacles)
{
	ASSERT_NO_FATAL_FAILURE(prepareObstacles());
	auto own = wall(7, -1);
	own.hidden = true;
	addLive(own);
	auto enemy = wall(8, -1);
	enemy.hidden = true;
	enemy.casterSide = BattleSide::DEFENDER;
	addLive(enemy);
	ASSERT_EQ(battle()->getAllObstacles().size(), 2u);
	ASSERT_EQ(callback->battleGetAllObstacles().size(), 1u);
	HypotheticBattle model(environment.get(), callback);
	const auto visible = model.getAllObstacles();
	ASSERT_EQ(visible.size(), 1u);
	EXPECT_EQ(visible.front()->uniqueID, 7);
}
