/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
namespace
{
class GraveDustEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GraveDustEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class GraveDustSnapshotTest : public BattleTestFixture {};
TEST_F(GraveDustSnapshotTest, MaterializedChildrenKeepDustAfterParentAndLiveClearBeforeFirstExplicitQuery)
{
	startGame();
	startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr);
	beginCombat();
	int64_t damage = 15;
	live->damage(damage);
	ASSERT_EQ(damage, 15);
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(live->unitId(), dust);
	const auto originalHealth = live->getAvailableHealth();
	auto environment = std::make_shared<GraveDustEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	EXPECT_FALSE(parent->initialExportSubject()); // No ALL_KNOWING promotion.
	auto parentUnit = parent->getForUpdate(live->unitId());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto childUnit = child->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto siblingUnit = sibling->getForUpdate(live->unitId());
	ASSERT_EQ(child->battleGetUnitByID(live->unitId()), childUnit.get());
	battle()->removeUnitBonus(live->unitId(), dust);
	parent->removeUnitBonus(live->unitId(), dust);
	// No explicit child coefficient/group query preceded removal of both ancestors' copies.
	EXPECT_EQ(battle::healingReceivedReduction(*childUnit), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingUnit), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*parentUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
	int64_t requested = 1000;
	EXPECT_EQ(childUnit->heal(requested, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 8);
	EXPECT_EQ(childUnit->getAvailableHealth(), originalHealth + 8);
	EXPECT_EQ(siblingUnit->getAvailableHealth(), originalHealth);
	EXPECT_EQ(parentUnit->getAvailableHealth(), originalHealth);
	EXPECT_EQ(live->getAvailableHealth(), originalHealth);
	child->removeUnitBonus(live->unitId(), dust);
	EXPECT_EQ(battle::healingReceivedReduction(*childUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingUnit), 5000);
}
}
