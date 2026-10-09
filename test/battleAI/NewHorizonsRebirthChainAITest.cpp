/*
 * NewHorizonsRebirthChainAITest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/NewHorizonsRebirthChainFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"

namespace
{
class ChainEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ChainEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ChainCallback final : public CBattleCallback
{
public:
	explicit ChainCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
};
}

class NewHorizonsRebirthChainAITest : public NewHorizonsRebirthChainFixture {};

TEST_F(NewHorizonsRebirthChainAITest, LiveAndDetachedSecondAppearanceAgreeAndBranchesKeepIndependentQuota)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * first = firstReborn();
	ASSERT_NE(first, nullptr);
	const auto originalHP = first->getRebirthOriginalAggregateHP();
	const auto id = first->unitId();
	ChainEnvironment environment(gameState());
	auto callback = std::make_shared<ChainCallback>(battle()->getSidePlayer(BattleSide::DEFENDER));
	callback->onBattleStarted(battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback->getBattle(BattleID(0)));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto snapshot = child->captureElementalRebirthSource(*child->battleGetUnitByID(id));
	ASSERT_TRUE(snapshot);
	ASSERT_TRUE(snapshot->chain);
	auto wounded = child->getForUpdate(id);
	int64_t damage = 1;
	wounded->damage(damage);
	EXPECT_EQ(wounded->getRebirthOriginalAggregateHP(), originalHP);
	damage = wounded->getAvailableHealth();
	wounded->damage(damage);
	const auto secondId = child->projectElementalRebirth(child->battleGetUnitByID(id), *snapshot, true, false, false);
	ASSERT_TRUE(secondId);
	const auto * second = child->battleGetUnitByID(*secondId);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(second->getAvailableHealth(), originalHP / 4);
	EXPECT_EQ(second->getRebirthOriginalAggregateHP(), 0);
	EXPECT_TRUE(child->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(parent->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_TRUE(first->alive());
	EXPECT_FALSE(child->captureElementalRebirthSource(*second));
	EXPECT_FALSE(child->projectElementalRebirth(child->battleGetUnitByID(id), *snapshot, true, false, false));
	auto grandchild = std::make_shared<HypotheticBattle>(&environment, child);
	EXPECT_TRUE(grandchild->getRebirthChainUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(grandchild->getRebirthChainUsed(BattleSide::ATTACKER));
	injure(first, 1);
	injure(first, first->getAvailableHealth());
	EXPECT_TRUE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	bool matched = false;
	for(const auto * live : battle()->battleGetAllUnits(false))
		if(live->alive() && live->isSummoned())
		{
			EXPECT_EQ(live->getAvailableHealth(), second->getAvailableHealth());
			EXPECT_EQ(live->getRebirthOriginalAggregateHP(), second->getRebirthOriginalAggregateHP());
			matched = true;
		}
	EXPECT_TRUE(matched);
}
