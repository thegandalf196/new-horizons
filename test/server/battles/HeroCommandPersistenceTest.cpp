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
#include "../../../lib/GameLibrary.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/serializer/CMemorySerializer.h"

class HeroCommandPersistenceTest : public HeroCommandFixture {};

TEST_F(HeroCommandPersistenceTest, WarMachinesAndEnemiesAreNotRecipients)
{
	prepareCommands();
	auto * machine = addStack(BattleSide::ATTACKER, creatureByName("ballista"), BattleHex(70), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 10);
	ASSERT_TRUE(machine->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_TRUE(issue(HeroCommand::AGGRESSIVE));
	EXPECT_TRUE(machine->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_TRUE(enemy->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	EXPECT_FALSE(battle()->battleActiveUnit()->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
}

TEST_F(HeroCommandPersistenceTest, LateArrivalsRequireDoctrineSwitchToReceiveItsEffects)
{
	prepareCommands();
	ASSERT_TRUE(issue(HeroCommand::AGGRESSIVE));
	auto * late = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 10);
	EXPECT_TRUE(late->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::AGGRESSIVE);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::AGGRESSIVE));
	ASSERT_TRUE(issue(HeroCommand::DEFENSIVE));
	EXPECT_EQ(late->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->size(), 2u);
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
	ASSERT_TRUE(issue(HeroCommand::AGGRESSIVE));
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::ADVANCE));
	const auto activeID = battle()->getActiveStackID();
	const auto speed = battle()->battleActiveUnit()->getMovementRange();
	const auto round = battle()->getRound();

	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(beforeBattle);

	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = CMemorySerializer::deepCopy(*battle(), replica.get());
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
	EXPECT_EQ(restored->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::ADVANCE);
	EXPECT_EQ(restored->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::AGGRESSIVE);
	EXPECT_TRUE(restored->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(restored->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::DEFENSIVE));

	BattleNextRound next;
	next.battleID = BattleID(0);
	restoredHandler->sendAndApply(next);
	EXPECT_EQ(restored->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_EQ(restored->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::AGGRESSIVE);
	EXPECT_LT(restored->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_TRUE(restored->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::DEFENSIVE));
	EXPECT_TRUE(restoredHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::DEFENSIVE)));
	EXPECT_EQ(restored->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::DEFENSIVE);

	// Continuing the replica did not mutate the original battle or its budget.
	EXPECT_EQ(battle()->battleGetActiveDoctrine(BattleSide::ATTACKER), HeroCommand::AGGRESSIVE);
	EXPECT_EQ(battle()->battleActiveUnit()->getMovementRange(), speed);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
}
