/*
 * NewHorizonsPursuitMarchTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
constexpr auto LOGISTICS = "new-horizons:logistics";
constexpr auto PURSUIT = "new-horizons:logistics.pursuitMarch";

class PursuitRecordingServer : public RecordingGameServer
{
public:
	std::vector<SetMovePoints> recoveries;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * movement = dynamic_cast<const SetMovePoints *>(&pack);
			movement && movement->pursuitMarchUseDay)
			recoveries.push_back(*movement);
		RecordingGameServer::applyPack(pack);
	}
};
}

class NewHorizonsPursuitMarchTest : public HeroCommandFixture
{
protected:
	PursuitRecordingServer pursuitServer;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(const auto & perk : perks["skills"][LOGISTICS]["perks"].Vector())
			if(perk["id"].String() == PURSUIT)
			{
				ASSERT_EQ(perk["effect"]["status"].String(), "active");
				found = true;
			}
		ASSERT_TRUE(found);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}

	void prepare(bool selected = true)
	{
		startGame();
		pursuitServer.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(pursuitServer, gameState());
		const SecondarySkill skill(SecondarySkill::decode(LOGISTICS));
		ASSERT_GE(skill.getNum(), 0);
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({LOGISTICS, "new-horizons:logistics.scouting"});
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(selected)
			attackerSideHero->applyPerkSelection({LOGISTICS, PURSUIT});
		ASSERT_EQ(attackerSideHero->hasActivePerk(LOGISTICS, PURSUIT), selected);
		ASSERT_TRUE(attackerSideHero->usesNewHorizonsMovement());
	}

	void finish(BattleSide winner = BattleSide::ATTACKER, EBattleResult result = EBattleResult::NORMAL)
	{
		startBattle();
		beginCombat();
		auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
		gameHandler->queries->addQuery(query);
		if(result == EBattleResult::NORMAL)
			gameHandler->battles->cheatBattleVictory(battle()->sideToPlayer(winner));
		else
		{
			const auto losingSide = battle()->otherSide(winner);
			const auto losingPlayer = battle()->sideToPlayer(losingSide);
			// Retreat and surrender are authenticated against the current activation.
			// Advance through accepted Defend actions rather than forging that turn.
			for(int actions = 0; actions < 8 && battle()->battleActiveUnit()
				&& battle()->battleActiveUnit()->unitSide() != losingSide; ++actions)
			{
				const auto * active = battle()->battleActiveUnit();
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
					battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
			}
			ASSERT_NE(battle()->battleActiveUnit(), nullptr);
			ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), losingSide);
			if(result == EBattleResult::SURRENDER)
			{
				const int cost = battle()->battleGetSurrenderCost(losingPlayer);
				ASSERT_GE(cost, 0);
				gameHandler->giveResource(losingPlayer, EGameResID::GOLD, cost);
			}
			else
				ASSERT_TRUE(battle()->battleCanFlee(losingPlayer));
			const auto action = result == EBattleResult::SURRENDER
				? BattleAction::makeSurrender(losingSide) : BattleAction::makeRetreat(losingSide);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), losingPlayer, action));
		}
		ASSERT_TRUE(query->result);
		for(const auto player : {PlayerColor(0), PlayerColor(1)})
		{
			auto dialog = gameHandler->queries->topQuery(player);
			if(dialog && dialog->getType() == QueryType::BattleDialog)
				ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
		}
	}

	void verifyVictory(EBattleResult result)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		gameHandler->setMovePoints(attackerSideHero->id, 10);
		const int expected = attackerSideHero->movementPointsLimit() / 10;
		const int day = gameState()->getCalendar().getCurrentDay();
		ASSERT_GT(expected, 0);
		ASSERT_NO_FATAL_FAILURE(finish(BattleSide::ATTACKER, result));
		EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 10 + expected);
		EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), day);
		ASSERT_EQ(pursuitServer.recoveries.size(), 1u);
		EXPECT_EQ(pursuitServer.recoveries.front().val, 10 + expected);
		EXPECT_EQ(pursuitServer.recoveries.front().pursuitMarchUseDay, day);
	}
};

TEST_F(NewHorizonsPursuitMarchTest, ActualVictoryRecoversAndAtomicallyStampsOncePerDay)
{
	verifyVictory(EBattleResult::NORMAL);
}

TEST_F(NewHorizonsPursuitMarchTest, OpponentRetreatStillQualifiesAsVictory)
{
	verifyVictory(EBattleResult::ESCAPE);
}

TEST_F(NewHorizonsPursuitMarchTest, OpponentSurrenderStillQualifiesAsVictory)
{
	verifyVictory(EBattleResult::SURRENDER);
}

TEST_F(NewHorizonsPursuitMarchTest, PriorSameDayUseBlocksActualVictoryRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int day = gameState()->getCalendar().getCurrentDay();
	gameHandler->setMovePoints(attackerSideHero->id, 0);
	SetMovePoints prior(attackerSideHero->id, attackerSideHero->movementPointsLimit() / 10);
	prior.pursuitMarchUseDay = day;
	gameHandler->sendAndApply(prior);
	gameHandler->setMovePoints(attackerSideHero->id, 5);
	pursuitServer.recoveries.clear();
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 5);
	EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), day);
	EXPECT_TRUE(pursuitServer.recoveries.empty());
}

TEST_F(NewHorizonsPursuitMarchTest, NewCalendarDayPermitsRecoveryWithoutClearingHistoricalStamp)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int day = gameState()->getCalendar().getCurrentDay();
	gameHandler->setMovePoints(attackerSideHero->id, 0);
	SetMovePoints prior(attackerSideHero->id, attackerSideHero->movementPointsLimit() / 10);
	prior.pursuitMarchUseDay = day;
	gameHandler->sendAndApply(prior);
	NewTurn next;
	next.day = day + 1;
	gameHandler->sendAndApply(next);
	ASSERT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), day);
	gameHandler->setMovePoints(attackerSideHero->id, 0);
	pursuitServer.recoveries.clear();
	const int expected = attackerSideHero->movementPointsLimit() / 10;
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), expected);
	EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), day + 1);
	ASSERT_EQ(pursuitServer.recoveries.size(), 1u);
}

TEST_F(NewHorizonsPursuitMarchTest, RecoveryCapsAtMissingMovement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int maximum = attackerSideHero->movementPointsLimit();
	gameHandler->setMovePoints(attackerSideHero->id, maximum - 1);
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), maximum);
	EXPECT_TRUE(attackerSideHero->hasUsedNewHorizonsPursuitMarchToday(gameState()->getCalendar().getCurrentDay()));
}

TEST_F(NewHorizonsPursuitMarchTest, OverCapVictoryDoesNotReduceMovementOrSpendDailyUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int before = attackerSideHero->movementPointsLimit() + 50;
	gameHandler->setMovePoints(attackerSideHero->id, before);
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), before);
	EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), -1);
	EXPECT_TRUE(pursuitServer.recoveries.empty());
}

TEST_F(NewHorizonsPursuitMarchTest, FullMovementVictoryDoesNotSpendDailyUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int maximum = attackerSideHero->movementPointsLimit();
	gameHandler->setMovePoints(attackerSideHero->id, maximum);
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), maximum);
	EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), -1);
	EXPECT_TRUE(pursuitServer.recoveries.empty());
}

TEST_F(NewHorizonsPursuitMarchTest, InactiveWinnerAndLosingSelectedHeroReceiveNoRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto loserId = attackerSideHero->id;
	gameHandler->setMovePoints(loserId, 0);
	ASSERT_NO_FATAL_FAILURE(finish(BattleSide::DEFENDER));
	EXPECT_TRUE(pursuitServer.recoveries.empty());
	EXPECT_EQ(gameState()->getHero(loserId), nullptr);
}

TEST_F(NewHorizonsPursuitMarchTest, UnselectedRankDoesNotRecover)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	gameHandler->setMovePoints(attackerSideHero->id, 0);
	ASSERT_NO_FATAL_FAILURE(finish());
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 0);
	EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), -1);
}

TEST_F(NewHorizonsPursuitMarchTest, PacketRejectsWrongDayAndAmountBeforeEitherMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	gameHandler->setMovePoints(attackerSideHero->id, 0);
	const int day = gameState()->getCalendar().getCurrentDay();
	SetMovePoints restore(attackerSideHero->id, attackerSideHero->movementPointsLimit() / 10);
	restore.pursuitMarchUseDay = day + 1;
	EXPECT_THROW(gameState()->apply(restore), std::runtime_error);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 0);
	EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), -1);
	restore.pursuitMarchUseDay = day;
	++restore.val;
	EXPECT_THROW(gameState()->apply(restore), std::runtime_error);
	EXPECT_EQ(attackerSideHero->movementPointsRemaining(), 0);
	EXPECT_EQ(attackerSideHero->getNewHorizonsPursuitMarchLastUseDay(), -1);
	--restore.val;
	gameState()->apply(restore);
	EXPECT_THROW(gameState()->apply(restore), std::runtime_error);
}

TEST_F(NewHorizonsPursuitMarchTest, CurrentSaveAndPacketPersistUseAndGuardOlderFormats)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setNewHorizonsPursuitMarchLastUseDay(19);
	const auto copy = CMemorySerializer::deepCopy(*attackerSideHero, gameState().get());
	ASSERT_NE(copy, nullptr);
	EXPECT_EQ(copy->getNewHorizonsPursuitMarchLastUseDay(), 19);
	CMemorySerializer oldHero;
	oldHero.oser.version = ESerializationVersion::NEW_HORIZONS_OVERWATCH;
	EXPECT_THROW(oldHero.oser & *attackerSideHero, std::runtime_error);
	EXPECT_TRUE(oldHero.extractBuffer().empty());
	SetMovePoints packet(attackerSideHero->id, 20);
	packet.pursuitMarchUseDay = 19;
	CMemorySerializer wire;
	wire.oser & packet;
	SetMovePoints restored;
	wire.iser & restored;
	EXPECT_EQ(restored.pursuitMarchUseDay, 19);
	EXPECT_EQ(restored.val, 20);
	CMemorySerializer oldPacket;
	oldPacket.oser.version = ESerializationVersion::NEW_HORIZONS_OVERWATCH;
	EXPECT_THROW(oldPacket.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldPacket.extractBuffer().empty());
	attackerSideHero->setNewHorizonsPursuitMarchLastUseDay(-1);
	CMemorySerializer previous;
	previous.oser.version = ESerializationVersion::NEW_HORIZONS_OVERWATCH;
	previous.iser.version = ESerializationVersion::NEW_HORIZONS_OVERWATCH;
	previous.iser.cb = gameState().get();
	previous.oser & attackerSideHero;
	std::unique_ptr<CGHeroInstance> previousHero;
	previous.iser & previousHero;
	ASSERT_NE(previousHero, nullptr);
	EXPECT_EQ(previousHero->getNewHorizonsPursuitMarchLastUseDay(), -1);
}
