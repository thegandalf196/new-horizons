/*
 * NewHorizonsBattleAITimingTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"

#include <functional>
#include <stdexcept>

namespace
{
class TimingEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit TimingEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class TimingCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> actions;
	std::function<void()> onSubmit;
	TimingCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override
	{
		actions.push_back(action);
		if(onSubmit)
			onSubmit();
	}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override
	{
		throw std::runtime_error("Unexpected Hero Action in timing-only healing-tent fixture");
	}
};
}

class NewHorizonsBattleAITimingTest : public HeroCommandFixture
{
protected:
	CStack * tent = nullptr;
	std::shared_ptr<TimingEnvironment> environment;
	std::shared_ptr<TimingCallback> callback;
	std::unique_ptr<CBattleAI> ai;

	void prepare()
	{
		useCommands = false;
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		tent = addStack(BattleSide::ATTACKER, creatureByName("core:firstAidTent"), BattleHex(2, 2), 1);
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 100);
		addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 100);
		beginCombat();
		ASSERT_TRUE(tent->isFirstAidTent());
		ASSERT_TRUE(tent->hasBonusOfType(BonusType::SIEGE_WEAPON));
		ASSERT_TRUE(tent->hasBonusOfType(BonusType::HEALER));
		callback = std::make_shared<TimingCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<TimingEnvironment>(gameState());
		ai = std::make_unique<CBattleAI>();
		ai->initBattleInterface(environment, callback);
		startTiming();
	}

	void startTiming()
	{
		ai->battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
	}

	CBattleAI::TimingSummary summary() const
	{
		return ai->getTimingSummary().value();
	}
};

TEST(NewHorizonsBattleAITimingCountersTest, UnstartedAIHasNoBattleSummary)
{
	CBattleAI ai;
	EXPECT_FALSE(ai.getTimingSummary().has_value());
}

TEST_F(NewHorizonsBattleAITimingTest, HealingTentEarlyReturnCountsOnceAndPreservesActionAndWaitFlag)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(summary().activeStackCalls, 0u);
	EXPECT_FALSE(callback->waitTillRealize);
	ai->activeStack(BattleID(0), tent);
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_EQ(callback->actions.front().actionType, EActionType::DEFEND);
	EXPECT_EQ(callback->actions.front().stackNumber, tent->unitId());
	EXPECT_EQ(callback->actions.front().side, BattleSide::ATTACKER);
	const auto timing = summary();
	EXPECT_EQ(timing.activeStackCalls, 1u);
	EXPECT_EQ(timing.activeStackInFlight, 0u);
	EXPECT_EQ(timing.activeStackTotalMicroseconds, timing.activeStackMaxMicroseconds);
	EXPECT_FALSE(timing.finished);
	EXPECT_FALSE(timing.reported);
	EXPECT_FALSE(callback->waitTillRealize);
}

TEST_F(NewHorizonsBattleAITimingTest, ThrowingSubmissionStillClosesExactlyOneCallbackScope)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	callback->onSubmit = [] { throw std::runtime_error("Expected diagnostic fixture failure"); };
	EXPECT_THROW(ai->activeStack(BattleID(0), tent), std::runtime_error);
	EXPECT_EQ(callback->actions.size(), 1u);
	const auto timing = summary();
	EXPECT_EQ(timing.activeStackCalls, 1u);
	EXPECT_EQ(timing.activeStackInFlight, 0u);
	EXPECT_EQ(timing.activeStackTotalMicroseconds, timing.activeStackMaxMicroseconds);
}

TEST_F(NewHorizonsBattleAITimingTest, BattleEndDefersSummaryUntilInFlightCallbackReturnsAndDoesNotRepeat)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	callback->onSubmit = [&]
	{
		ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
		EXPECT_TRUE(summary().finished);
		EXPECT_FALSE(summary().reported);
		EXPECT_EQ(summary().activeStackInFlight, 1u);
	};
	ai->activeStack(BattleID(0), tent);
	const auto timing = summary();
	EXPECT_TRUE(timing.finished);
	EXPECT_TRUE(timing.reported);
	EXPECT_EQ(timing.activeStackCalls, 1u);
	EXPECT_EQ(timing.activeStackInFlight, 0u);
	ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
	EXPECT_EQ(summary().battleWallMicroseconds, timing.battleWallMicroseconds);
	EXPECT_EQ(summary().activeStackTotalMicroseconds, timing.activeStackTotalMicroseconds);
}

TEST_F(NewHorizonsBattleAITimingTest, ResetKeepsOldInFlightScopeOutOfNewBattleCounters)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	callback->onSubmit = [&]
	{
		ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
		startTiming();
		EXPECT_EQ(summary().activeStackCalls, 0u);
	};
	ai->activeStack(BattleID(0), tent);
	EXPECT_EQ(summary().activeStackCalls, 0u);
	EXPECT_EQ(summary().activeStackInFlight, 0u);
	EXPECT_EQ(summary().activeStackTotalMicroseconds, 0u);
	EXPECT_FALSE(summary().finished);
	callback->onSubmit = {};
	ai->activeStack(BattleID(0), tent);
	EXPECT_EQ(summary().activeStackCalls, 1u);
}

TEST_F(NewHorizonsBattleAITimingTest, EndingCallbackCanDestroyOwnerWithoutInvalidatingTimingScope)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	callback->onSubmit = [&]
	{
		ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
		EXPECT_EQ(summary().activeStackInFlight, 1u);
		ai.reset();
	};
	auto * activeAI = ai.get();
	EXPECT_NO_THROW(activeAI->activeStack(BattleID(0), tent));
	EXPECT_FALSE(ai);
	EXPECT_EQ(callback->actions.size(), 1u);
}

TEST_F(NewHorizonsBattleAITimingTest, ObservedRoundNotificationsAreDistinctAndResetWithBattleStart)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ai->battleNewRound(BattleID(0));
	ai->battleNewRound(BattleID(0));
	EXPECT_EQ(summary().roundsObserved, 1u);
	advanceRound();
	ai->battleNewRound(BattleID(0));
	EXPECT_EQ(summary().roundsObserved, 2u);
	startTiming();
	EXPECT_EQ(summary().roundsObserved, 0u);
	EXPECT_EQ(summary().activeStackCalls, 0u);
}
