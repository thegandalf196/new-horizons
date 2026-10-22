/*
 * OptionalHydraForecastTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/CRandomGenerator.h"
#include <array>

class OptionalHydraForecastTest : public BattleTestFixture
{
protected:
	CStack * subject = nullptr;
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<HypotheticBattle> parent;
	using SavedState = std::array<std::vector<std::byte>, 5>;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the core-only test profile";
		startGame();
		startBattle();
		subject = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
		addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	}
	SavedState savedState()
	{
		return {gameState()->saveToMemory(), projectedHitTest::serialized(*battle()),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}
};

TEST_F(OptionalHydraForecastTest, CoreActivationForecastRequiresNoOptionalHydraAndKeepsNativeAndSibling)
{
	const auto before = savedState();
	const auto parentState = parent->getForUpdate(subject->unitId())->save();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	ASSERT_NO_THROW(child->nextTurn(subject->unitId(), BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_TRUE(child->getForUpdate(subject->unitId())->alive());
	EXPECT_EQ(parent->getForUpdate(subject->unitId())->save(), parentState);
	EXPECT_EQ(sibling->getForUpdate(subject->unitId())->save(), parentState);
	EXPECT_EQ(savedState(), before);
}

TEST_F(OptionalHydraForecastTest, CoreCapacityRoundForecastRequiresNoOptionalHydraAndKeepsNativeAndSibling)
{
	const auto before = savedState();
	const auto parentState = parent->getForUpdate(subject->unitId())->save();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto unit = child->getForUpdate(subject->unitId());
	// Exercise the existing generic capacity-ledger transition, not a forged
	// Hydra marker or altered spell configuration in the core-only profile.
	const auto originalMaximum = unit->getMaxHealth();
	const auto health = unit->getAvailableHealth();
	unit->preserveCreatureHealthOnCapacityIncrease();
	Bonus capacity(BonusDuration::N_TURNS, BonusType::STACK_HEALTH, BonusSource::HERO_SPECIAL, 50, BonusSourceID());
	capacity.turnsRemain = 1;
	unit->addUnitBonus({capacity});
	unit->normalizeCapacityHealth();
	ASSERT_EQ(unit->getMaxHealth(), originalMaximum + 50);
	ASSERT_TRUE(unit->health.isCapacityHealthTracking());
	ASSERT_GT(unit->getCapacityHealthReferenceMax(), 0);
	ASSERT_EQ(unit->save()["state"]["capacityHealthReferenceMax"].Integer(), originalMaximum);
	ASSERT_EQ(unit->getAvailableHealth(), health);
	ASSERT_NO_THROW(unit->advanceTimedRound());
	EXPECT_EQ(unit->save()["state"]["capacityHealthReferenceMax"].Integer(), 0);
	EXPECT_FALSE(unit->health.isCapacityHealthTracking());
	EXPECT_EQ(unit->getMaxHealth(), originalMaximum);
	EXPECT_EQ(unit->getCapacityHealthReferenceMax(), originalMaximum);
	EXPECT_EQ(unit->getAvailableHealth(), health);
	EXPECT_EQ(parent->getForUpdate(subject->unitId())->save(), parentState);
	EXPECT_EQ(sibling->getForUpdate(subject->unitId())->save(), parentState);
	EXPECT_EQ(savedState(), before);
}
