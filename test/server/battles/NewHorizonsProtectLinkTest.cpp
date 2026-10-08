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

TEST_F(NewHorizonsProtectLinkTest, ProposedAdjacentPairExistsBeforeAnOrderIsIssued)
{
	preparePair();
	ASSERT_TRUE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::PROTECT,
		{protector->unitId(), ward->unitId()}));
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
	const auto link = newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId());
	ASSERT_TRUE(link);
	EXPECT_EQ(link->protectorUnitId, protector->unitId());
	EXPECT_EQ(link->wardUnitId, ward->unitId());
	EXPECT_EQ(link->protectorHead, protector->getPosition());
	EXPECT_EQ(link->protectorRear, protector->occupiedHex());
	EXPECT_EQ(link->wardHead, ward->getPosition());
	EXPECT_EQ(link->wardRear, ward->occupiedHex());
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsProtectLinkTest, ProposedPairRejectsInvalidSidesIdsAndTargets)
{
	preparePair();
	const auto preview = [this](BattleSide side, uint32_t first, uint32_t second)
	{
		return newHorizonsProtectLink::proposedLink(*battle(), side, first, second);
	};
	EXPECT_FALSE(preview(BattleSide::NONE, protector->unitId(), ward->unitId()));
	EXPECT_FALSE(preview(static_cast<BattleSide>(127), protector->unitId(), ward->unitId()));
	EXPECT_FALSE(preview(BattleSide::DEFENDER, protector->unitId(), ward->unitId()));
	EXPECT_FALSE(preview(BattleSide::ATTACKER, std::numeric_limits<uint32_t>::max(), ward->unitId()));
	EXPECT_FALSE(preview(BattleSide::ATTACKER, protector->unitId(), std::numeric_limits<uint32_t>::max()));
	EXPECT_FALSE(preview(BattleSide::ATTACKER, protector->unitId(), protector->unitId()));
	EXPECT_FALSE(preview(BattleSide::ATTACKER, protector->unitId(), enemy->unitId()));
	EXPECT_FALSE(preview(BattleSide::ATTACKER, enemy->unitId(), ward->unitId()));
	moveTo(ward, BattleHex(120));
	ASSERT_FALSE(footprintsTouch(protector->getHexes(), ward->getHexes()));
	EXPECT_FALSE(preview(BattleSide::ATTACKER, protector->unitId(), ward->unitId()));
}

TEST_F(NewHorizonsProtectLinkTest, ProposedPairTracksCurrentDoubleWideFootprints)
{
	preparePair();
	const auto original = newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId());
	ASSERT_TRUE(original);
	const BattleHex protectorDestination(54);
	const BattleHex wardDestination(56);
	ASSERT_TRUE(footprintsTouch(protector->getHexes(protectorDestination), ward->getHexes()));
	moveTo(protector, protectorDestination);
	ASSERT_TRUE(footprintsTouch(protector->getHexes(), ward->getHexes(wardDestination)));
	moveTo(ward, wardDestination);
	const auto moved = newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId());
	ASSERT_TRUE(moved);
	EXPECT_EQ(moved->protectorHead, protectorDestination);
	EXPECT_EQ(moved->protectorRear, protector->occupiedHex());
	EXPECT_EQ(moved->wardHead, wardDestination);
	EXPECT_EQ(moved->wardRear, ward->occupiedHex());
	EXPECT_NE(*moved, *original);
	EXPECT_FALSE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsProtectLinkTest, RepeatedProposedPairReadbackDoesNotSpendOrMutate)
{
	preparePair();
	const auto allowancesBefore = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto protectorBefore = protector->acquireState()->save();
	const auto wardBefore = ward->acquireState()->save();
	const auto normalSpellPointsBefore = attackerSideHero->getNormalSpellPoints();
	const auto bufferSpellPointsBefore = attackerSideHero->getBufferSpellPoints();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto actionsBefore = server.startedActions.size();
	const auto ordersBefore = server.orderStateUpdates.size();
	const auto first = newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId());
	ASSERT_TRUE(first);
	for(int i = 0; i < 5; ++i)
		EXPECT_EQ(newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
			protector->unitId(), ward->unitId()), first);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowancesBefore);
	EXPECT_EQ(protector->acquireState()->save(), protectorBefore);
	EXPECT_EQ(ward->acquireState()->save(), wardBefore);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalSpellPointsBefore);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferSpellPointsBefore);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(server.startedActions.size(), actionsBefore);
	EXPECT_EQ(server.orderStateUpdates.size(), ordersBefore);
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::PROTECT));
	ASSERT_TRUE(issueProtect()); // Readback retained the actual action opportunity.
	EXPECT_TRUE(newHorizonsProtectLink::activeLink(*battle(), BattleSide::ATTACKER));
}

TEST_F(NewHorizonsProtectLinkTest, ProposedPairRequiresAnAvailableHeroAction)
{
	preparePair();
	ASSERT_TRUE(newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId()));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_FALSE(battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::PROTECT,
		{protector->unitId(), ward->unitId()}));
	EXPECT_FALSE(newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId()));
}

TEST_F(NewHorizonsProtectLinkTest, DeadWardCannotBeProposed)
{
	preparePair();
	auto deadWard = ward->acquireState();
	int64_t lethalDamage = deadWard->getAvailableHealth();
	ASSERT_GT(lethalDamage, 0);
	deadWard->damage(lethalDamage);
	ASSERT_FALSE(deadWard->alive());
	BattleUnitsChanged killed;
	killed.battleID = BattleID(0);
	killed.changedStacks.emplace_back(ward->unitId(), UnitChanges::EOperation::UPDATE);
	killed.changedStacks.back().data = deadWard->save();
	killed.changedStacks.back().healthDelta = -lethalDamage;
	gameHandler->sendAndApply(killed);
	EXPECT_FALSE(newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId()));
	EXPECT_FALSE(newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		ward->unitId(), protector->unitId()));
}

TEST_F(NewHorizonsProtectLinkTest, LegacyBattleDoesNotExposeProposedPair)
{
	preparePair(true);
	EXPECT_FALSE(newHorizonsProtectLink::proposedLink(*battle(), BattleSide::ATTACKER,
		protector->unitId(), ward->unitId()));
}
