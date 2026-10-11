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
#include "../../AI/BattleAI/BattleEvaluator.h"
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
	std::vector<BattleAction> heroActions;
	unsigned retreatDecisionCalls = 0;
	bool acceptHeroActions = false;
	std::function<void()> onSubmit;
	TimingCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		++retreatDecisionCalls;
		return std::nullopt;
	}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override
	{
		actions.push_back(action);
		if(onSubmit)
			onSubmit();
	}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		if(!acceptHeroActions)
			throw std::runtime_error("Unexpected Hero Action in timing-only healing-tent fixture");
		heroActions.push_back(action);
		if(onSubmit)
			onSubmit();
	}
};
}

class NewHorizonsBattleAITimingTest : public HeroCommandFixture
{
protected:
	CStack * tent = nullptr;
	CStack * ordinary = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<TimingEnvironment> environment;
	std::shared_ptr<TimingCallback> callback;
	std::unique_ptr<CBattleAI> ai;

	void prepare(bool spellbook = false, bool adjacentEnemy = false)
	{
		useCommands = false;
		startGame();
		if(spellbook)
		{
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
			setTestSpellPointTotal(attackerSideHero, 100);
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		tent = addStack(BattleSide::ATTACKER, creatureByName("core:firstAidTent"), BattleHex(2, 2), 1);
		ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
			adjacentEnemy ? BattleHex(6, 5) : BattleHex(13, 5), 100);
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

	void expectStageClosure(const CBattleAI::TimingSummary & timing) const
	{
		const uint64_t classified = timing.evaluatorConstruction.totalMicroseconds
			+ timing.stackActionSelection.totalMicroseconds + timing.heroAction.totalMicroseconds
			+ timing.directUnitSubmission.totalMicroseconds;
		EXPECT_LE(classified, timing.activeStackTotalMicroseconds);
		EXPECT_EQ(classified + timing.unclassifiedMicroseconds, timing.activeStackTotalMicroseconds);
		const uint64_t selectionClassified = timing.selectionCreatureSpell.totalMicroseconds
			+ timing.selectionMovementSearch.totalMicroseconds + timing.selectionTargetExchange.totalMicroseconds;
		EXPECT_LE(selectionClassified, timing.stackActionSelection.totalMicroseconds);
		EXPECT_EQ(selectionClassified + timing.selectionRemainderMicroseconds,
			timing.stackActionSelection.totalMicroseconds);
		const uint64_t movementClassified = timing.movementFindMove.totalMicroseconds
			+ timing.movementGoTowards.totalMicroseconds;
		EXPECT_LE(movementClassified, timing.selectionMovementSearch.totalMicroseconds);
		EXPECT_EQ(movementClassified + timing.movementRemainderMicroseconds,
			timing.selectionMovementSearch.totalMicroseconds);
		EXPECT_EQ(timing.movementFindMove.calls + timing.movementGoTowards.calls,
			timing.selectionMovementSearch.calls);
		EXPECT_LE(timing.movementFindMoveExchangeSubset.totalMicroseconds,
			timing.movementFindMove.totalMicroseconds);
		EXPECT_LE(timing.movementGoBlockingSubset.totalMicroseconds,
			timing.movementGoTowards.totalMicroseconds);
	}

	void expectSameSelectionWithAndWithoutTiming(bool adjacentEnemy)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(false, adjacentEnemy));
		const auto ourHealth = ordinary->getAvailableHealth();
		const auto enemyHealth = enemy->getAvailableHealth();
		const auto ourPosition = ordinary->getPosition();
		const auto enemyPosition = enemy->getPosition();
		const auto normalSpellPoints = attackerSideHero->getNormalSpellPoints();
		const auto bufferSpellPoints = attackerSideHero->getBufferSpellPoints();
		BattleEvaluator control(environment, callback, ordinary, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		BattleEvaluator measured(environment, callback, ordinary, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		const auto expected = control.selectStackAction(ordinary);
		BattleEvaluator::SelectionTiming timing;
		const auto actual = measured.selectStackAction(ordinary, &timing);
		EXPECT_EQ(actual.side, expected.side);
		EXPECT_EQ(actual.stackNumber, expected.stackNumber);
		EXPECT_EQ(actual.actionType, expected.actionType);
		EXPECT_EQ(actual.spell, expected.spell);
		EXPECT_EQ(actual.perfectMoment, expected.perfectMoment);
		EXPECT_EQ(actual.archerySkirmisherAttack, expected.archerySkirmisherAttack);
		const auto actualTargets = actual.getTarget(battle());
		const auto expectedTargets = expected.getTarget(battle());
		ASSERT_EQ(actualTargets.size(), expectedTargets.size());
		for(size_t index = 0; index < actualTargets.size(); ++index)
		{
			EXPECT_EQ(actualTargets[index].hexValue, expectedTargets[index].hexValue);
			EXPECT_EQ(actualTargets[index].unitValue, expectedTargets[index].unitValue);
		}
		EXPECT_EQ(timing.creatureSpell.calls, 1u);
		EXPECT_GE(timing.movementSearch.calls, 1u);
		EXPECT_EQ(timing.movementDetails.findMove.calls, 1u);
		EXPECT_EQ(timing.movementDetails.findMove.calls + timing.movementDetails.goTowards.calls,
			timing.movementSearch.calls);
		EXPECT_LE(timing.movementDetails.findMove.totalMicroseconds + timing.movementDetails.goTowards.totalMicroseconds,
			timing.movementSearch.totalMicroseconds);
		EXPECT_LE(timing.movementDetails.findMoveExchange.totalMicroseconds,
			timing.movementDetails.findMove.totalMicroseconds);
		EXPECT_LE(timing.movementDetails.goTowardsBlocking.totalMicroseconds,
			timing.movementDetails.goTowards.totalMicroseconds);
		if(!adjacentEnemy)
		{
			EXPECT_GE(timing.movementDetails.findMoveExchange.calls, 1u);
		}
		if(adjacentEnemy)
		{
			EXPECT_GE(timing.targetExchange.calls, 1u);
		}
		EXPECT_EQ(ordinary->getAvailableHealth(), ourHealth);
		EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
		EXPECT_EQ(ordinary->getPosition(), ourPosition);
		EXPECT_EQ(enemy->getPosition(), enemyPosition);
		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), normalSpellPoints);
		EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), bufferSpellPoints);
		EXPECT_TRUE(callback->actions.empty());
		EXPECT_TRUE(callback->heroActions.empty());
		EXPECT_EQ(callback->retreatDecisionCalls, 0u);
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
	EXPECT_EQ(timing.evaluatorConstruction.calls, 0u);
	EXPECT_EQ(timing.stackActionSelection.calls, 0u);
	EXPECT_EQ(timing.heroAction.calls, 0u);
	EXPECT_EQ(timing.directUnitSubmission.calls, 1u);
	EXPECT_EQ(timing.selectionCreatureSpell.calls, 0u);
	EXPECT_EQ(timing.selectionMovementSearch.calls, 0u);
	EXPECT_EQ(timing.selectionTargetExchange.calls, 0u);
	EXPECT_EQ(timing.movementFindMove.calls, 0u);
	EXPECT_EQ(timing.movementGoTowards.calls, 0u);
	EXPECT_EQ(timing.movementFindMoveExchangeSubset.calls, 0u);
	EXPECT_EQ(timing.movementGoBlockingSubset.calls, 0u);
	expectStageClosure(timing);
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
	EXPECT_EQ(timing.directUnitSubmission.calls, 1u);
	expectStageClosure(timing);
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
		EXPECT_EQ(summary().directUnitSubmission.calls, 0u);
	};
	ai->activeStack(BattleID(0), tent);
	const auto timing = summary();
	EXPECT_TRUE(timing.finished);
	EXPECT_TRUE(timing.reported);
	EXPECT_EQ(timing.activeStackCalls, 1u);
	EXPECT_EQ(timing.activeStackInFlight, 0u);
	EXPECT_EQ(timing.directUnitSubmission.calls, 1u);
	expectStageClosure(timing);
	ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
	EXPECT_EQ(summary().battleWallMicroseconds, timing.battleWallMicroseconds);
	EXPECT_EQ(summary().activeStackTotalMicroseconds, timing.activeStackTotalMicroseconds);
	EXPECT_EQ(summary().directUnitSubmission.calls, timing.directUnitSubmission.calls);
	EXPECT_EQ(summary().directUnitSubmission.totalMicroseconds, timing.directUnitSubmission.totalMicroseconds);
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
	EXPECT_EQ(summary().directUnitSubmission.calls, 0u);
	EXPECT_EQ(summary().directUnitSubmission.totalMicroseconds, 0u);
	EXPECT_EQ(summary().unclassifiedMicroseconds, 0u);
	EXPECT_EQ(summary().selectionCreatureSpell.calls, 0u);
	EXPECT_EQ(summary().selectionMovementSearch.calls, 0u);
	EXPECT_EQ(summary().selectionTargetExchange.calls, 0u);
	EXPECT_EQ(summary().selectionRemainderMicroseconds, 0u);
	EXPECT_EQ(summary().movementFindMove.calls, 0u);
	EXPECT_EQ(summary().movementGoTowards.calls, 0u);
	EXPECT_EQ(summary().movementFindMoveExchangeSubset.calls, 0u);
	EXPECT_EQ(summary().movementGoBlockingSubset.calls, 0u);
	EXPECT_EQ(summary().movementFindMove.totalMicroseconds, 0u);
	EXPECT_EQ(summary().movementGoTowards.totalMicroseconds, 0u);
	EXPECT_EQ(summary().movementFindMoveExchangeSubset.totalMicroseconds, 0u);
	EXPECT_EQ(summary().movementGoBlockingSubset.totalMicroseconds, 0u);
	EXPECT_EQ(summary().movementRemainderMicroseconds, 0u);
	EXPECT_FALSE(summary().finished);
	callback->onSubmit = {};
	ai->activeStack(BattleID(0), tent);
	EXPECT_EQ(summary().activeStackCalls, 1u);
	EXPECT_EQ(summary().directUnitSubmission.calls, 1u);
	expectStageClosure(summary());
}

TEST_F(NewHorizonsBattleAITimingTest, EndingCallbackCanDestroyOwnerWithoutInvalidatingTimingScope)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	callback->onSubmit = [&]
	{
		ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
		EXPECT_EQ(summary().activeStackInFlight, 1u);
		EXPECT_EQ(summary().directUnitSubmission.calls, 0u);
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

TEST_F(NewHorizonsBattleAITimingTest, OrdinaryCallbackClassifiesConstructionSelectionAndDirectSubmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ai->autobattlePreferences.enableSpellsUsage = false;
	ASSERT_FALSE(battle()->battleUsesHeroCommands());
	ai->activeStack(BattleID(0), ordinary);
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_EQ(callback->actions.front().stackNumber, ordinary->unitId());
	EXPECT_EQ(callback->retreatDecisionCalls, 1u);
	const auto timing = summary();
	EXPECT_EQ(timing.activeStackCalls, 1u);
	EXPECT_EQ(timing.evaluatorConstruction.calls, 1u);
	EXPECT_EQ(timing.stackActionSelection.calls, 1u);
	EXPECT_EQ(timing.selectionCreatureSpell.calls, 1u);
	EXPECT_GE(timing.selectionMovementSearch.calls, 1u);
	EXPECT_EQ(timing.heroAction.calls, 0u);
	EXPECT_EQ(timing.directUnitSubmission.calls, 1u);
	EXPECT_FALSE(callback->waitTillRealize);
	expectStageClosure(timing);
}

TEST_F(NewHorizonsBattleAITimingTest, HeroActionStageIncludesInternalSubmissionWithoutDoubleCounting)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	callback->acceptHeroActions = true;
	ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	ai->activeStack(BattleID(0), ordinary);
	ASSERT_EQ(callback->heroActions.size(), 1u);
	ASSERT_TRUE(callback->actions.empty());
	EXPECT_EQ(callback->retreatDecisionCalls, 0u);
	const auto timing = summary();
	EXPECT_EQ(timing.activeStackCalls, 1u);
	EXPECT_EQ(timing.evaluatorConstruction.calls, 1u);
	EXPECT_EQ(timing.stackActionSelection.calls, 1u);
	EXPECT_EQ(timing.heroAction.calls, 1u);
	// Hero submissions occur inside the measured attempt and are not counted
	// a second time in the separately measured direct unit submission stage.
	EXPECT_EQ(timing.directUnitSubmission.calls, callback->actions.size());
	EXPECT_FALSE(callback->waitTillRealize);
	expectStageClosure(timing);
}

TEST_F(NewHorizonsBattleAITimingTest, DistantSelectionTimingPreservesUntimedDecisionAndLiveState)
{
	ASSERT_NO_FATAL_FAILURE(expectSameSelectionWithAndWithoutTiming(false));
}

TEST_F(NewHorizonsBattleAITimingTest, AdjacentSelectionTimingPreservesUntimedDecisionAndLiveState)
{
	ASSERT_NO_FATAL_FAILURE(expectSameSelectionWithAndWithoutTiming(true));
}

TEST_F(NewHorizonsBattleAITimingTest, OrdinaryThrowingSubmissionRetainsClosedSelectionPartition)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ai->autobattlePreferences.enableSpellsUsage = false;
	callback->onSubmit = []() { throw std::runtime_error("Expected diagnostic fixture failure"); };
	EXPECT_THROW(ai->activeStack(BattleID(0), ordinary), std::runtime_error);
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_EQ(callback->retreatDecisionCalls, 1u);
	const auto timing = summary();
	EXPECT_EQ(timing.activeStackInFlight, 0u);
	EXPECT_EQ(timing.stackActionSelection.calls, 1u);
	EXPECT_EQ(timing.selectionCreatureSpell.calls, 1u);
	EXPECT_GE(timing.selectionMovementSearch.calls, 1u);
	EXPECT_EQ(timing.directUnitSubmission.calls, 1u);
	expectStageClosure(timing);
}

TEST_F(NewHorizonsBattleAITimingTest, LongGroundMovementTimingPreservesUntimedDestinationAndLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	PotentialTargets targets(ordinary, cache, model);
	BattleHexArray destinations;
	destinations.insert(BattleHex(11, 5));
	ASSERT_FALSE(ordinary->hasBonusOfType(BonusType::FLYING));
	const auto reachability = battle()->getReachability(ordinary);
	ASSERT_TRUE(reachability.isReachable(BattleHex(11, 5)));
	ASSERT_FALSE(battle()->battleGetAvailableHexes(reachability, ordinary, false).contains(BattleHex(11, 5)));
	const auto ourHealth = ordinary->getAvailableHealth();
	const auto enemyHealth = enemy->getAvailableHealth();
	const auto ourPosition = ordinary->getPosition();
	const auto enemyPosition = enemy->getPosition();
	BattleEvaluator control(environment, callback, ordinary, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	BattleEvaluator measured(environment, callback, ordinary, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	const auto expected = control.goTowardsNearest(ordinary, destinations, targets);
	BattleMovementTiming timing;
	const auto actual = measured.goTowardsNearest(ordinary, destinations, targets, &timing);
	EXPECT_EQ(actual.actionType, EActionType::WALK);
	EXPECT_EQ(actual.side, expected.side);
	EXPECT_EQ(actual.stackNumber, expected.stackNumber);
	EXPECT_EQ(actual.actionType, expected.actionType);
	EXPECT_EQ(actual.spell, expected.spell);
	EXPECT_EQ(actual.perfectMoment, expected.perfectMoment);
	EXPECT_EQ(actual.archerySkirmisherAttack, expected.archerySkirmisherAttack);
	const auto actualTargets = actual.getTarget(battle());
	const auto expectedTargets = expected.getTarget(battle());
	ASSERT_EQ(actualTargets.size(), expectedTargets.size());
	for(size_t index = 0; index < actualTargets.size(); ++index)
	{
		EXPECT_EQ(actualTargets[index].hexValue, expectedTargets[index].hexValue);
		EXPECT_EQ(actualTargets[index].unitValue, expectedTargets[index].unitValue);
	}
	EXPECT_EQ(timing.findMove.calls, 0u);
	EXPECT_EQ(timing.goTowards.calls, 1u);
	EXPECT_EQ(timing.findMoveExchange.calls, 0u);
	EXPECT_GE(timing.goTowardsBlocking.calls, 1u);
	EXPECT_LE(timing.goTowardsBlocking.totalMicroseconds, timing.goTowards.totalMicroseconds);
	EXPECT_EQ(ordinary->getAvailableHealth(), ourHealth);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
	EXPECT_EQ(ordinary->getPosition(), ourPosition);
	EXPECT_EQ(enemy->getPosition(), enemyPosition);
	EXPECT_TRUE(callback->actions.empty());
	EXPECT_TRUE(callback->heroActions.empty());
}

TEST_F(NewHorizonsBattleAITimingTest, EmptyMovementDestinationClosesParentWithoutBlockingQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	PotentialTargets targets(ordinary, cache, model);
	BattleEvaluator evaluator(environment, callback, ordinary, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	BattleMovementTiming timing;
	const auto actual = evaluator.goTowardsNearest(ordinary, BattleHexArray{}, targets, &timing);
	EXPECT_EQ(actual.actionType, EActionType::DEFEND);
	EXPECT_EQ(actual.stackNumber, ordinary->unitId());
	EXPECT_EQ(timing.goTowards.calls, 1u);
	EXPECT_EQ(timing.findMove.calls, 0u);
	EXPECT_EQ(timing.findMoveExchange.calls, 0u);
	EXPECT_EQ(timing.goTowardsBlocking.calls, 0u);
	EXPECT_EQ(timing.goTowardsBlocking.totalMicroseconds, 0u);
	EXPECT_TRUE(callback->actions.empty());
}
