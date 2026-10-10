/*
 * NewHorizonsHoldAnchorUITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../client/battle/NewHorizonsHoldAnchor.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"

class NewHorizonsHoldAnchorUITest : public HeroCommandFixture
{
protected:
	CStack * held = nullptr;
	CStack * single = nullptr;
	CStack * enemy = nullptr;

	void prepareAnchors()
	{
		startGame();
		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
		held = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(70), 10);
		single = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(74), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 10);
		ASSERT_NE(held, nullptr);
		ASSERT_NE(single, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_TRUE(held->doubleWide());
		ASSERT_FALSE(single->doubleWide());
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = held->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	void moveTo(BattleHex destination)
	{
		BattleStackMoved moved;
		moved.battleID = BattleID(0);
		moved.stack = held->unitId();
		moved.tilesToMove.insert(destination);
		gameHandler->sendAndApply(moved);
	}

	bool shows(uint32_t id) const
	{
		for(const auto & footprint : newHorizonsHoldAnchor::activeAnchors(*battle()))
			if(footprint.unitId == id)
				return true;
		return false;
	}
};

TEST_F(NewHorizonsHoldAnchorUITest, PaidOrderShowsHeadAndRearWithoutChangingTheReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepareAnchors());
	EXPECT_TRUE(newHorizonsHoldAnchor::activeAnchors(*battle()).empty());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE);
	ASSERT_TRUE(order);
	const auto before = *order;
	const auto healthBefore = held->getAvailableHealth();
	const auto footprints = newHorizonsHoldAnchor::activeAnchors(*battle());
	ASSERT_EQ(footprints.size(), 2);
	for(const auto & footprint : footprints)
	{
		if(footprint.unitId == held->unitId())
		{
			EXPECT_EQ(footprint.head, held->getPosition());
			EXPECT_EQ(footprint.rear, held->occupiedHex());
		}
		else
		{
			EXPECT_EQ(footprint.unitId, single->unitId());
			EXPECT_EQ(footprint.head, single->getPosition());
			EXPECT_EQ(footprint.rear, BattleHex::INVALID);
		}
	}
	EXPECT_TRUE(shows(held->unitId()));
	EXPECT_TRUE(shows(single->unitId()));
	EXPECT_FALSE(shows(enemy->unitId()));
	const auto after = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE);
	ASSERT_TRUE(after);
	EXPECT_EQ(*after, before);
	EXPECT_EQ(held->getAvailableHealth(), healthBefore);
	auto * lateArrival = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(76), 10);
	ASSERT_NE(lateArrival, nullptr);
	EXPECT_FALSE(shows(lateArrival->unitId()));
}

TEST_F(NewHorizonsHoldAnchorUITest, MovementBreaksAnchorAndReturningDoesNotRestoreIt)
{
	ASSERT_NO_FATAL_FAILURE(prepareAnchors());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_TRUE(shows(held->unitId()));
	moveTo(BattleHex(54));
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE);
	ASSERT_TRUE(order);
	ASSERT_TRUE(order->containsHoldBroken(held->unitId()));
	EXPECT_FALSE(shows(held->unitId()));
	EXPECT_TRUE(shows(single->unitId()));
	moveTo(BattleHex(70));
	EXPECT_FALSE(shows(held->unitId()));
}

TEST_F(NewHorizonsHoldAnchorUITest, RoundExpiryRemovesAllAnchorPositions)
{
	ASSERT_NO_FATAL_FAILURE(prepareAnchors());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_FALSE(newHorizonsHoldAnchor::activeAnchors(*battle()).empty());
	advanceRound();
	EXPECT_TRUE(newHorizonsHoldAnchor::activeAnchors(*battle()).empty());
}

TEST_F(NewHorizonsHoldAnchorUITest, CurrentControllerSuppressesAnchorUntilControlReturns)
{
	ASSERT_NO_FATAL_FAILURE(prepareAnchors());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	const auto before = newHorizonsHoldAnchor::activeAnchors(*battle());
	ASSERT_TRUE(shows(held->unitId()));
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	held->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(held), PlayerColor(1));
	EXPECT_FALSE(shows(held->unitId()));
	EXPECT_TRUE(shows(single->unitId()));
	held->removeBonus(control);
	ASSERT_EQ(battle()->battleGetOwner(held), PlayerColor(0));
	EXPECT_EQ(newHorizonsHoldAnchor::activeAnchors(*battle()), before);
}

TEST_F(NewHorizonsHoldAnchorUITest, DeathRemovesTheOccupiedAnchorWithoutHidingSurvivors)
{
	ASSERT_NO_FATAL_FAILURE(prepareAnchors());
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	ASSERT_TRUE(shows(held->unitId()));
	auto dead = held->acquireState();
	int64_t lethal = dead->getAvailableHealth();
	ASSERT_GT(lethal, 0);
	dead->damage(lethal);
	ASSERT_FALSE(dead->alive());
	BattleUnitsChanged killed;
	killed.battleID = BattleID(0);
	killed.changedStacks.emplace_back(held->unitId(), UnitChanges::EOperation::UPDATE);
	killed.changedStacks.back().data = dead->save();
	killed.changedStacks.back().healthDelta = -lethal;
	gameHandler->sendAndApply(killed);
	EXPECT_FALSE(shows(held->unitId()));
	EXPECT_TRUE(shows(single->unitId()));
}
