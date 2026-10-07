/*
 * NewHorizonsProtectLinkTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../client/battle/NewHorizonsProtectLink.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"

namespace
{
bool footprintsTouch(const BattleHexArray & first, const BattleHexArray & second)
{
	for(const auto & firstHex : first)
		for(const auto & secondHex : second)
			if(BattleHex::getDistance(firstHex, secondHex) == 1)
				return true;
	return false;
}
}

class NewHorizonsProtectLinkTest : public HeroCommandFixture
{
protected:
	CStack * protector = nullptr;
	CStack * ward = nullptr;
	CStack * enemy = nullptr;

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
	}

	void preparePair(bool legacy = false)
	{
		useCommands = !legacy;
		startGame();
		startBattle();
		clearStartingUnits();

		// Both selected stacks are double-wide, so the readback must describe each
		// current head and rear footprint rather than only its target hex.
		protector = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(70), 10);
		ward = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(72), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 10);
		ASSERT_NE(protector, nullptr);
		ASSERT_NE(ward, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_TRUE(protector->doubleWide());
		ASSERT_TRUE(ward->doubleWide());

		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = protector->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	bool issueProtect()
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
				protector->unitId(), ward->unitId()));
	}

	void moveTo(CStack * stack, BattleHex destination)
	{
		BattleStackMoved moved;
		moved.battleID = BattleID(0);
		moved.stack = stack->unitId();
		moved.tilesToMove.insert(destination);
		gameHandler->sendAndApply(moved);
	}
};

TEST_F(NewHorizonsProtectLinkTest, AcceptedPairTracksCurrentDoubleWideFootprints)
{
	preparePair();
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));

	ASSERT_TRUE(issueProtect());
	auto link = newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER);
	ASSERT_TRUE(link);
	EXPECT_EQ(link->protectorUnitId, protector->unitId());
	EXPECT_EQ(link->wardUnitId, ward->unitId());
	EXPECT_EQ(link->protectorHead, protector->getPosition());
	EXPECT_EQ(link->protectorRear, protector->occupiedHex());
	EXPECT_EQ(link->wardHead, ward->getPosition());
	EXPECT_EQ(link->wardRear, ward->occupiedHex());

	// Reposition each unit through the existing movement result path while keeping
	// the pair adjacent; the readback should follow the live footprints, not issue-time
	// positions. These are nearby legal footprints with no intervening separation.
	const BattleHex protectorDestination(54);
	ASSERT_TRUE(footprintsTouch(protector->getHexes(protectorDestination), ward->getHexes()));
	moveTo(protector, protectorDestination);
	link = newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER);
	ASSERT_TRUE(link);
	EXPECT_EQ(link->protectorHead, protectorDestination);
	EXPECT_EQ(link->protectorRear, protector->occupiedHex());

	const BattleHex wardDestination(56);
	ASSERT_TRUE(footprintsTouch(protector->getHexes(), ward->getHexes(wardDestination)));
	moveTo(ward, wardDestination);
	link = newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER);
	ASSERT_TRUE(link);
	EXPECT_EQ(link->protectorHead, protector->getPosition());
	EXPECT_EQ(link->protectorRear, protector->occupiedHex());
	EXPECT_EQ(link->wardHead, wardDestination);
	EXPECT_EQ(link->wardRear, ward->occupiedHex());
}

TEST_F(NewHorizonsProtectLinkTest, ExhaustedInterceptionRemovesLink)
{
	preparePair();
	ASSERT_TRUE(issueProtect());
	ASSERT_TRUE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));

	ASSERT_TRUE(battle()->interceptHeroOrderProtect(BattleSide::ATTACKER));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::PROTECT);
	ASSERT_TRUE(state);
	EXPECT_EQ(state->protectInterceptionsConsumed, state->protectInterceptionLimit);
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsProtectLinkTest, BrokenPairDoesNotReturnAfterReunion)
{
	preparePair();
	ASSERT_TRUE(issueProtect());
	ASSERT_TRUE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));

	moveTo(ward, BattleHex(120));
	const auto separated = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::PROTECT);
	ASSERT_TRUE(separated);
	EXPECT_TRUE(separated->protectBroken);
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));

	moveTo(ward, BattleHex(72));
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsProtectLinkTest, ExpiredOrderHasNoLink)
{
	preparePair();
	ASSERT_TRUE(issueProtect());
	ASSERT_TRUE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));

	advanceRound();
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::PROTECT));
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsProtectLinkTest, DeadMemberHasNoLink)
{
	preparePair();
	ASSERT_TRUE(issueProtect());
	ASSERT_TRUE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));

	auto deadProtector = protector->acquireState();
	int64_t lethalDamage = deadProtector->getAvailableHealth();
	ASSERT_GT(lethalDamage, 0);
	deadProtector->damage(lethalDamage);
	ASSERT_FALSE(deadProtector->alive());

	BattleUnitsChanged killed;
	killed.battleID = BattleID(0);
	killed.changedStacks.emplace_back(protector->unitId(), UnitChanges::EOperation::UPDATE);
	killed.changedStacks.back().data = deadProtector->save();
	killed.changedStacks.back().healthDelta = -lethalDamage;
	gameHandler->sendAndApply(killed);

	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsProtectLinkTest, LegacyBattleDoesNotExposeProtectLink)
{
	preparePair(true);
	EXPECT_FALSE(battle()->battleUsesHeroCommands());
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::NONE));
}
