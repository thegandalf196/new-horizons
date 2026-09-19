/*
 * HeroCommandPersistenceTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class HeroCommandPersistenceTest : public HeroCommandFixture {};

TEST_F(HeroCommandPersistenceTest, LegacyAdvanceNormalizationRemovesIdentityAndRoundBonus)
{
	prepareCommands();
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.heroCommandUsed = true;
	side.activeOrder = HeroCommand::ADVANCE;
	Bonus legacySpeed(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::HERO_COMMAND, 25, BonusSourceID());
	legacySpeed.turnsRemain = 1;
	auto * active = battle()->getStack(battle()->getActiveStackID());
	ASSERT_NE(active, nullptr);
	active->addNewBonus(std::make_shared<Bonus>(legacySpeed));
	ASSERT_FALSE(battle()->battleActiveUnit()->getAllBonuses(
		Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());

	battle()->normalizeLegacyHeroCommandState();

	EXPECT_TRUE(side.heroCommandUsed);
	EXPECT_EQ(side.activeOrder, HeroCommand::NONE);
	EXPECT_TRUE(battle()->battleActiveUnit()->getAllBonuses(
		Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
}

TEST_F(HeroCommandPersistenceTest, WarMachinesAndEnemiesAreNotRecipients)
{
	prepareCommands();
	auto * machine = addStack(BattleSide::ATTACKER, creatureByName("ballista"), BattleHex(70), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 10);
	ASSERT_TRUE(machine->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(machine->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_TRUE(enemy->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_FALSE(battle()->battleActiveUnit()->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
}

TEST_F(HeroCommandPersistenceTest, LateArrivalsReceiveOnlyTheNextRoundOrder)
{
	prepareCommands();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	auto * late = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 10);
	EXPECT_TRUE(late->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(late->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->size(), 1u);
}

TEST_F(HeroCommandPersistenceTest, NoLivingOrdinaryRecipientMakesEveryCommandUnavailable)
{
	prepareCommands();
	auto * machine = addStack(BattleSide::ATTACKER, creatureByName("ballista"), BattleHex(70), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllStacks())
	{
		if(unit->unitSide() == BattleSide::ATTACKER && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	}
	gameHandler->sendAndApply(remove);
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = machine->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	// Read-only transitional-state guard, not a claim that a finished battle
	// normally offers another hero action before its result is processed.
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::ADVANCE,
		HeroCommand::AGGRESSIVE, HeroCommand::DEFENSIVE})
		EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, command));
}

TEST_F(HeroCommandPersistenceTest, FullBattleStartPacketRestoresEffectsBudgetAndLegalContinuation)
{
	startGame();
	// Independent game/army graph: never attach a deserialized battle to the
	// original authoritative armies, whose lifetime and bonus links must survive.
	const auto beforeBattle = gameState()->saveToMemory();
	startBattle();
	beginCombat();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	const auto activeID = battle()->getActiveStackID();
	const auto speed = battle()->battleActiveUnit()->getMovementRange();
	const auto round = battle()->getRound();

	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(beforeBattle);

	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = replica.get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	EXPECT_EQ(incoming.info->getHeroCommandRules(), battle()->getHeroCommandRules());
	EXPECT_EQ(incoming.info->getRound(), round);

	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto restoredHandler = std::make_shared<CGameHandler>(restoredServer, replica);
	restoredHandler->sendAndApply(incoming);
	auto * restored = replica->getBattle(BattleID(0));
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getActiveStackID(), activeID);
	EXPECT_EQ(restored->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_EQ(restored->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(restored->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_TRUE(restored->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(restored->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_FALSE(restored->battleActiveUnit()->getAllBonuses(
		Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());

	BattleNextRound next;
	next.battleID = BattleID(0);
	restoredHandler->sendAndApply(next);
	EXPECT_EQ(restored->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(restored->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(restored->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_TRUE(restored->battleActiveUnit()->getAllBonuses(
		Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_TRUE(restored->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_TRUE(restoredHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	EXPECT_EQ(restored->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);

	// Continuing the replica did not mutate the original battle or its budget.
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(battle()->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}
