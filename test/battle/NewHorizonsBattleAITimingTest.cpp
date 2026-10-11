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
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/MapDifficulty.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>

namespace
{
void expectEffectWorkBounds(const HypotheticLifecycleTiming::EffectWorkTiming & work,
	uint64_t parentCalls, uint64_t parentMicroseconds, bool removal)
{
	uint64_t measured = 0;
	for(const auto * stage : {&work.stackedQuery, &work.rawQuery, &work.snapshotMaterialization,
		&work.selectiveRemovalQuery, &work.removalMutation, &work.roundAging})
	{
		measured += stage->totalMicroseconds;
		EXPECT_LE(stage->calls, parentCalls);
		if(parentCalls == 0)
			EXPECT_EQ(stage->totalMicroseconds, 0u);
	}
	EXPECT_LE(measured, parentMicroseconds);
	EXPECT_EQ(work.stackedQuery.calls, parentCalls);
	EXPECT_EQ(work.rawQuery.calls, parentCalls);
	EXPECT_EQ(work.snapshotMaterialization.calls, parentCalls);
	EXPECT_EQ(work.selectiveRemovalQuery.calls, removal ? parentCalls : 0u);
	EXPECT_EQ(work.removalMutation.calls, removal ? parentCalls : 0u);
	if(removal)
		EXPECT_EQ(work.roundAging.calls, 0u);
}

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
	explicit TimingCallback(PlayerColor player = PlayerColor(0)) : CBattleCallback(player, nullptr) {}
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
		const auto subsets = [](const CBattleAI::StageTiming & expiry, const CBattleAI::StageTiming & gambler,
			const CBattleAI::StageTiming & roundTimed, const CBattleAI::StageTiming & activation,
			const CBattleAI::StageTiming & round)
		{
			EXPECT_LE(expiry.totalMicroseconds + gambler.totalMicroseconds, activation.totalMicroseconds);
			EXPECT_EQ(expiry.calls, gambler.calls);
			EXPECT_LE(expiry.calls, activation.calls);
			EXPECT_LE(roundTimed.totalMicroseconds, round.totalMicroseconds);
		};
		subsets(timing.movementExchangeActivationExpiryRemoval, timing.movementExchangeActivationGamblerRemoval,
			timing.movementExchangeRoundTimedUpdates, timing.movementExchangeActivationStart, timing.movementExchangeRoundAdvance);
		subsets(timing.bestTargetWaitedActivationExpiryRemoval, timing.bestTargetWaitedActivationGamblerRemoval,
			timing.bestTargetWaitedRoundTimedUpdates, timing.bestTargetWaitedActivationStart, timing.bestTargetWaitedRoundAdvance);
		subsets(timing.bestTargetNormalActivationExpiryRemoval, timing.bestTargetNormalActivationGamblerRemoval,
			timing.bestTargetNormalRoundTimedUpdates, timing.bestTargetNormalActivationStart, timing.bestTargetNormalRoundAdvance);
		expectEffectWorkBounds(timing.movementExchangeActivationExpiryWork, timing.movementExchangeActivationExpiryRemoval.calls,
			timing.movementExchangeActivationExpiryRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(timing.movementExchangeActivationGamblerWork, timing.movementExchangeActivationGamblerRemoval.calls,
			timing.movementExchangeActivationGamblerRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(timing.movementExchangeRoundTimedWork, timing.movementExchangeRoundTimedUpdates.calls,
			timing.movementExchangeRoundTimedUpdates.totalMicroseconds, false);
		expectEffectWorkBounds(timing.bestTargetWaitedActivationExpiryWork, timing.bestTargetWaitedActivationExpiryRemoval.calls,
			timing.bestTargetWaitedActivationExpiryRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(timing.bestTargetWaitedActivationGamblerWork, timing.bestTargetWaitedActivationGamblerRemoval.calls,
			timing.bestTargetWaitedActivationGamblerRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(timing.bestTargetWaitedRoundTimedWork, timing.bestTargetWaitedRoundTimedUpdates.calls,
			timing.bestTargetWaitedRoundTimedUpdates.totalMicroseconds, false);
		expectEffectWorkBounds(timing.bestTargetNormalActivationExpiryWork, timing.bestTargetNormalActivationExpiryRemoval.calls,
			timing.bestTargetNormalActivationExpiryRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(timing.bestTargetNormalActivationGamblerWork, timing.bestTargetNormalActivationGamblerRemoval.calls,
			timing.bestTargetNormalActivationGamblerRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(timing.bestTargetNormalRoundTimedWork, timing.bestTargetNormalRoundTimedUpdates.calls,
			timing.bestTargetNormalRoundTimedUpdates.totalMicroseconds, false);
		EXPECT_EQ(timing.bestTargetWaitedExchange.totalMicroseconds
			+ timing.bestTargetNormalExchange.totalMicroseconds + timing.bestTargetRemainderMicroseconds,
			timing.selectionBestTarget.totalMicroseconds);
		EXPECT_EQ(timing.bestTargetWaitedMembership.totalMicroseconds
			+ timing.bestTargetWaitedProjectionSetup.totalMicroseconds + timing.bestTargetWaitedTargetProbes.totalMicroseconds
			+ timing.bestTargetWaitedAppliedAttacks.totalMicroseconds + timing.bestTargetWaitedActivationStart.totalMicroseconds
			+ timing.bestTargetWaitedActivationEnd.totalMicroseconds + timing.bestTargetWaitedRoundAdvance.totalMicroseconds
			+ timing.bestTargetWaitedRemainderMicroseconds,
			timing.bestTargetWaitedBody.totalMicroseconds);
		EXPECT_EQ(timing.bestTargetNormalMembership.totalMicroseconds
			+ timing.bestTargetNormalProjectionSetup.totalMicroseconds + timing.bestTargetNormalTargetProbes.totalMicroseconds
			+ timing.bestTargetNormalAppliedAttacks.totalMicroseconds + timing.bestTargetNormalActivationStart.totalMicroseconds
			+ timing.bestTargetNormalActivationEnd.totalMicroseconds + timing.bestTargetNormalRoundAdvance.totalMicroseconds
			+ timing.bestTargetNormalRemainderMicroseconds,
			timing.bestTargetNormalBody.totalMicroseconds);
		EXPECT_EQ(timing.selectionTargetExchange.calls, timing.selectionBestTarget.calls
			+ timing.selectionSiegeDefense.calls + timing.selectionDefensiveStance.calls);
		EXPECT_EQ(timing.selectionTargetExchange.totalMicroseconds, timing.selectionBestTarget.totalMicroseconds
			+ timing.selectionSiegeDefense.totalMicroseconds + timing.selectionDefensiveStance.totalMicroseconds);
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
		expectTargetPartition(timing);
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
			EXPECT_EQ(timing.bestTarget.calls, 1u);
			EXPECT_EQ(timing.siegeDefense.calls, 0u);
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

	void expectTargetPartition(const BattleEvaluator::SelectionTiming & timing) const
	{
		EXPECT_EQ(timing.targetExchange.calls, timing.bestTarget.calls
			+ timing.siegeDefense.calls + timing.defensiveStance.calls);
		EXPECT_EQ(timing.targetExchange.totalMicroseconds, timing.bestTarget.totalMicroseconds
			+ timing.siegeDefense.totalMicroseconds + timing.defensiveStance.totalMicroseconds);
	}

	void expectEmptyTargetPartition(const CBattleAI::TimingSummary & timing) const
	{
		for(const auto * stage : {&timing.selectionBestTarget, &timing.selectionSiegeDefense,
			&timing.selectionDefensiveStance})
		{
			EXPECT_EQ(stage->calls, 0u);
			EXPECT_EQ(stage->totalMicroseconds, 0u);
		}
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
	expectEmptyTargetPartition(timing);
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
	expectEmptyTargetPartition(summary());
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
		expectEmptyTargetPartition(summary());
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

TEST_F(NewHorizonsBattleAITimingTest, OrdinaryBattleEndRetainsClosedTargetPartitionAfterSubmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ai->autobattlePreferences.enableSpellsUsage = false;
	callback->onSubmit = [&]
	{
		ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
		EXPECT_TRUE(summary().finished);
		EXPECT_FALSE(summary().reported);
		EXPECT_EQ(summary().activeStackInFlight, 1u);
	};
	ai->activeStack(BattleID(0), ordinary);
	ASSERT_EQ(callback->actions.size(), 1u);
	const auto timing = summary();
	EXPECT_TRUE(timing.finished);
	EXPECT_TRUE(timing.reported);
	EXPECT_EQ(timing.activeStackInFlight, 0u);
	EXPECT_EQ(timing.selectionBestTarget.calls, 1u);
	EXPECT_EQ(timing.selectionSiegeDefense.calls, 0u);
	expectStageClosure(timing);
	ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
	EXPECT_EQ(summary().selectionBestTarget.calls, timing.selectionBestTarget.calls);
	EXPECT_EQ(summary().selectionBestTarget.totalMicroseconds, timing.selectionBestTarget.totalMicroseconds);
	EXPECT_EQ(summary().selectionTargetExchange.totalMicroseconds, timing.selectionTargetExchange.totalMicroseconds);
}

TEST_F(NewHorizonsBattleAITimingTest, OrdinaryResetExcludesOldTargetPartitionFromNewSummary)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ai->autobattlePreferences.enableSpellsUsage = false;
	callback->onSubmit = [&]
	{
		ai->battleEnd(BattleID(0), nullptr, QueryID(-1));
		startTiming();
	};
	ai->activeStack(BattleID(0), ordinary);
	EXPECT_EQ(summary().activeStackCalls, 0u);
	EXPECT_EQ(summary().selectionTargetExchange.calls, 0u);
	EXPECT_EQ(summary().selectionTargetExchange.totalMicroseconds, 0u);
	const auto reset = summary();
	for(const auto * stage : {&reset.movementExchangeActivationStart, &reset.movementExchangeActivationEnd,
		&reset.movementExchangeRoundAdvance, &reset.bestTargetWaitedActivationStart,
		&reset.bestTargetWaitedActivationEnd, &reset.bestTargetWaitedRoundAdvance,
		&reset.bestTargetNormalActivationStart, &reset.bestTargetNormalActivationEnd,
		&reset.bestTargetNormalRoundAdvance, &reset.movementExchangeActivationExpiryRemoval,
		&reset.movementExchangeActivationGamblerRemoval, &reset.movementExchangeRoundTimedUpdates,
		&reset.bestTargetWaitedActivationExpiryRemoval, &reset.bestTargetWaitedActivationGamblerRemoval,
		&reset.bestTargetWaitedRoundTimedUpdates, &reset.bestTargetNormalActivationExpiryRemoval,
		&reset.bestTargetNormalActivationGamblerRemoval, &reset.bestTargetNormalRoundTimedUpdates})
	{
		EXPECT_EQ(stage->calls, 0u);
		EXPECT_EQ(stage->totalMicroseconds, 0u);
	}
	expectEmptyTargetPartition(summary());
	expectStageClosure(summary());
	callback->onSubmit = {};
	ai->activeStack(BattleID(0), ordinary);
	EXPECT_EQ(summary().activeStackCalls, 1u);
	EXPECT_EQ(summary().selectionBestTarget.calls, 1u);
	EXPECT_EQ(summary().selectionSiegeDefense.calls, 0u);
	expectStageClosure(summary());
}

class NewHorizonsTargetPartitionTest : public NewHorizonsBattleAITimingTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		// Keep the existing legacy-command timing scene but capture the actual
		// registered generic perk pool for the independent defensive branch.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
};

TEST_F(NewHorizonsTargetPartitionTest, DefensiveStancePartitionPreservesNullSinkDecisionAndLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto armorer = SecondarySkill::decode("new-horizons:armorer");
	ASSERT_GE(armorer, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(armorer), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:armorer", "new-horizons:armorer.pavise"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.pavise"));
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(12, 7), 3);
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, ordinary->getPosition()));
	ordinary->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -ordinary->getMovementRange(), BonusSourceID()));
	ASSERT_EQ(ordinary->getMovementRange(), 0);
	ASSERT_FALSE(ordinary->waitedThisTurn);
	ASSERT_FALSE(ordinary->defended());
	const auto ourHealth = ordinary->getAvailableHealth();
	const auto enemyHealth = enemy->getAvailableHealth();
	const auto shooterHealth = shooter->getAvailableHealth();
	const auto ourPosition = ordinary->getPosition();
	BattleEvaluator control(environment, callback, ordinary, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	BattleEvaluator measured(environment, callback, ordinary, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	const auto expected = control.selectStackAction(ordinary, nullptr);
	BattleEvaluator::SelectionTiming timing;
	const auto actual = measured.selectStackAction(ordinary, &timing);
	EXPECT_EQ(actual.toString(), expected.toString());
	EXPECT_EQ(actual.perfectMoment, expected.perfectMoment);
	EXPECT_EQ(actual.archerySkirmisherAttack, expected.archerySkirmisherAttack);
	EXPECT_EQ(timing.bestTarget.calls, 0u);
	EXPECT_EQ(timing.siegeDefense.calls, 0u);
	EXPECT_EQ(timing.defensiveStance.calls, 1u);
	EXPECT_EQ(timing.targetExchange.calls, 1u);
	expectTargetPartition(timing);
	EXPECT_EQ(ordinary->getAvailableHealth(), ourHealth);
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHealth);
	EXPECT_EQ(shooter->getAvailableHealth(), shooterHealth);
	EXPECT_EQ(ordinary->getPosition(), ourPosition);
	EXPECT_FALSE(ordinary->defended());
	EXPECT_FALSE(ordinary->waitedThisTurn);
	EXPECT_TRUE(callback->actions.empty());
	EXPECT_TRUE(callback->heroActions.empty());
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

class NewHorizonsExchangeReadParityTest : public NewHorizonsBattleAITimingTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		// These two controls exercise current commands, including the dynamic
		// Focus Fire damage guard, rather than the legacy timing-only profile.
		useCommands = true;
		HeroCommandFixture::mapLoaded(loaded);
	}

	void expectUnusedReadParity(bool stateful)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(false, true));
		auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 3), 50);
		auto * enemyShooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 7), 50);
		auto * controlled = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(6, 6), 50);
		ASSERT_TRUE(shooter->canShoot());
		ASSERT_TRUE(enemyShooter->canShoot());
		ASSERT_FALSE(ordinary->canShoot());
		ASSERT_TRUE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE));

		auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		std::map<uint32_t, JsonNode> liveBefore;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			const auto * live = dynamic_cast<const battle::CUnitState *>(unit);
			ASSERT_NE(live, nullptr);
			const auto snapshot = live->acquireState();
			liveBefore.emplace(unit->unitId(), snapshot->save());
			parent->getForUpdate(unit->unitId());
		}
		if(stateful)
		{
			auto projectedEnemy = parent->getForUpdate(enemy->unitId());
			ASSERT_GE(parent->battleGetRound(), 1);
			ASSERT_TRUE(newHorizonsFrozen::canApply(*projectedEnemy, parent->battleGetRound()))
				<< "round=" << parent->battleGetRound()
				<< " previous application=" << projectedEnemy->frozenLastAppliedRound();
			// addUnitBonus prepares and commits the Frozen receipt itself.
			projectedEnemy->addUnitBonus({newHorizonsFrozen::makeFrozenMarker(
				BonusSourceID(creatureByName("core:iceElemental")), parent->battleGetRound())});
			ASSERT_EQ(projectedEnemy->frozenLastAppliedRound(), parent->battleGetRound());
			projectedEnemy->applyNoQuarter(1, true);
			parent->getForUpdate(controlled->unitId())->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE,
				BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
			FocusFireState mark;
			mark.targetUnitId = enemy->unitId();
			mark.recipientUnitIds = {shooter->unitId()};
			mark.issuedRound = parent->battleGetRound();
			mark.rangedDamagePercent = 30;
			parent->setFocusFireState(BattleSide::ATTACKER, mark);
			HeroOrderState order;
			order.command = HeroCommand::FOCUS_FIRE;
			order.issuedRound = parent->battleGetRound();
			order.primaryTargetUnitId = enemy->unitId();
			parent->setHeroOrderState(BattleSide::ATTACKER, order);
			ASSERT_EQ(parent->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE),
				std::optional<HeroOrderState>(order));
			ASSERT_TRUE(order.scheduledFor(shooter->unitId(), parent->battleGetRound()));
			ASSERT_TRUE(parent->battleIsFocusFireRecipient(parent->battleGetUnitByID(shooter->unitId()),
				BattleSide::ATTACKER));
			ASSERT_TRUE(parent->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
			ASSERT_TRUE(newHorizonsFrozen::isFrozen(*projectedEnemy));
			ASSERT_TRUE(projectedEnemy->hasBonus(CSelector(newHorizonsOffense::isNoQuarterBonus)));
			ASSERT_NE(parent->battleGetActionController(parent->battleGetUnitByID(controlled->unitId())),
				battle()->battleGetActionController(controlled));
			ASSERT_GT(parent->battleTargetedRangedCommandPercent(parent->battleGetUnitByID(shooter->unitId()),
				projectedEnemy.get(), true), 0);
		}

		std::map<uint32_t, JsonNode> parentBefore;
		for(const auto * unit : parent->battleGetAllUnits(false))
			parentBefore.emplace(unit->unitId(), parent->getForUpdate(unit->unitId())->save());
		const auto markBefore = parent->getFocusFireState(BattleSide::ATTACKER);
		const auto ordersBefore = parent->getHeroOrderStates(BattleSide::ATTACKER);
		std::optional<float> expectedScore;
		std::optional<BattleAction> expectedAction;
		for(const bool unusedReads : {false, true})
		{
			auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
			for(const auto & [id, snapshot] : parentBefore)
				branch->getForUpdate(id);
			const auto * actor = branch->battleGetUnitByID(ordinary->unitId());
			DamageCache cache;
			cache.buildDamageCache(branch, BattleSide::ATTACKER);
			PotentialTargets targets(actor, cache, branch);
			ASSERT_FALSE(targets.possibleAttacks.empty());
			const auto attack = std::ranges::find_if(targets.possibleAttacks, [this](const AttackPossibility & candidate)
			{
				return candidate.attack.defender->unitId() == enemy->unitId();
			});
			ASSERT_NE(attack, targets.possibleAttacks.end());
			BattleExchangeEvaluator exchange(callback->getBattle(BattleID(0)), environment, 1.0f, 2);
			exchange.updateReachabilityMap(branch);
			const auto units = exchange.getExchangeUnits(*attack, 0, targets, branch);
			ASSERT_EQ(units.units.size(), 2u); // Normal's two-round forecast, not a shortened search.
			if(unusedReads)
			{
				// Reference only the deleted preprocessing. It never supplies a
				// target/score; the two-binary XML comparison checks the actual
				// old-versus-new production algorithm independently of this oracle.
				battle::Units ourStacks;
				for(const auto & [turn, queue] : units.units)
					for(const auto * unit : queue)
						if(!unit->isTurret() && branch->battleMatchOwner(attack->attack.attacker, unit, true)
							&& branch->getForUpdate(unit->unitId())->alive() && !vstd::contains(ourStacks, unit))
							ourStacks.push_back(unit);
				ASSERT_FALSE(ourStacks.empty());
				auto meleeAttackers = ourStacks;
				vstd::removeDuplicates(meleeAttackers);
				vstd::erase_if(meleeAttackers, [this](const battle::Unit * unit)
				{
					return callback->getBattle(BattleID(0))->battleCanShoot(unit);
				});
				ASSERT_FALSE(meleeAttackers.empty());
				ASSERT_LT(meleeAttackers.size(), ourStacks.size()); // Both melee and shooting queries occur.
			}
			const float score = exchange.evaluateExchange(*attack, 0, targets, cache, branch);
			ASSERT_TRUE(std::isfinite(score));
			ASSERT_NE(score, EvaluationResult::INEFFECTIVE_SCORE);
			BattleEvaluator selector(environment, callback, branch, cache, actor, PlayerColor(0), BattleID(0),
				BattleSide::ATTACKER, 1.0f, 2);
			const auto action = selector.selectStackAction(ordinary);
			ASSERT_EQ(action.stackNumber, ordinary->unitId());
			if(expectedScore)
			{
				EXPECT_EQ(std::bit_cast<uint32_t>(score), std::bit_cast<uint32_t>(*expectedScore));
				EXPECT_EQ(action.toString(), expectedAction->toString());
				EXPECT_EQ(action.perfectMoment, expectedAction->perfectMoment);
				EXPECT_EQ(action.archerySkirmisherAttack, expectedAction->archerySkirmisherAttack);
			}
			else
			{
				expectedScore = score;
				expectedAction = action;
				RecordProperty("exchangeScoreBits", std::to_string(std::bit_cast<uint32_t>(score)));
				RecordProperty("selectedAction", action.toString());
				RecordProperty("perfectMoment", action.perfectMoment ? "true" : "false");
				RecordProperty("archerySkirmisherAttack", action.archerySkirmisherAttack ? "true" : "false");
			}
			for(const auto & [id, snapshot] : parentBefore)
				EXPECT_EQ(branch->getForUpdate(id)->save(), snapshot);
			EXPECT_EQ(branch->getFocusFireState(BattleSide::ATTACKER), markBefore);
			EXPECT_EQ(branch->getHeroOrderStates(BattleSide::ATTACKER), ordersBefore);
		}
		for(const auto & [id, snapshot] : parentBefore)
			EXPECT_EQ(parent->getForUpdate(id)->save(), snapshot);
		EXPECT_EQ(parent->getFocusFireState(BattleSide::ATTACKER), markBefore);
		EXPECT_EQ(parent->getHeroOrderStates(BattleSide::ATTACKER), ordersBefore);
		if(stateful)
		{
			EXPECT_TRUE(newHorizonsFrozen::isFrozen(*parent->battleGetUnitByID(enemy->unitId())));
			EXPECT_TRUE(parent->battleGetUnitByID(enemy->unitId())->hasBonus(CSelector(newHorizonsOffense::isNoQuarterBonus)));
			EXPECT_NE(parent->battleGetActionController(parent->battleGetUnitByID(controlled->unitId())),
				battle()->battleGetActionController(controlled));
		}
		for(const auto & [id, snapshot] : liveBefore)
		{
			const auto * live = dynamic_cast<const battle::CUnitState *>(battle()->battleGetUnitByID(id));
			ASSERT_NE(live, nullptr);
			const auto current = live->acquireState();
			EXPECT_EQ(current->save(), snapshot);
		}
		EXPECT_TRUE(callback->actions.empty());
		EXPECT_TRUE(callback->heroActions.empty());
		EXPECT_EQ(callback->retreatDecisionCalls, 0u);
	}
};

TEST_F(NewHorizonsExchangeReadParityTest, MixedMeleeShootersNormalTwoRoundScoreAndActionMatchUnusedReads)
{
	ASSERT_NO_FATAL_FAILURE(expectUnusedReadParity(false));
}

TEST_F(NewHorizonsExchangeReadParityTest, FrozenNoQuarterControllerAndFocusFireForecastMatchUnusedReads)
{
	ASSERT_NO_FATAL_FAILURE(expectUnusedReadParity(true));
}

class NewHorizonsExchangeTimingTest : public NewHorizonsBattleAITimingTest
{
protected:
	void expectLifecycleSubsets(const BattleMovementTiming::ExchangeTiming & exchange) const
	{
		const auto & nested = exchange.lifecycleSubsets;
		EXPECT_LE(nested.activationExpiryRemoval.totalMicroseconds + nested.activationGamblerRemoval.totalMicroseconds,
			exchange.activationStart.totalMicroseconds);
		// These count measured calls, not completed activations or rounds.
		EXPECT_EQ(nested.activationExpiryRemoval.calls, nested.activationGamblerRemoval.calls);
		EXPECT_LE(nested.activationExpiryRemoval.calls, exchange.activationStart.calls);
		EXPECT_LE(nested.roundTimedUpdates.totalMicroseconds, exchange.roundAdvance.totalMicroseconds);
		expectEffectWorkBounds(nested.activationExpiryWork, nested.activationExpiryRemoval.calls,
			nested.activationExpiryRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(nested.activationGamblerWork, nested.activationGamblerRemoval.calls,
			nested.activationGamblerRemoval.totalMicroseconds, true);
		expectEffectWorkBounds(nested.roundTimedWork, nested.roundTimedUpdates.calls,
			nested.roundTimedUpdates.totalMicroseconds, false);
	}

	void expectBestTargetClosure(const BattleBestTargetTiming & timing) const
	{
		for(const auto pair : {std::pair{&timing.waitedExchange, &timing.waitedDetails},
			std::pair{&timing.normalExchange, &timing.normalDetails}})
		{
			const auto & exchange = *pair.second;
			expectLifecycleSubsets(exchange);
			EXPECT_EQ(exchange.body.calls, pair.first->calls);
			EXPECT_LE(exchange.body.totalMicroseconds, pair.first->totalMicroseconds);
			EXPECT_EQ(exchange.membership.totalMicroseconds + exchange.projectionSetup.totalMicroseconds
				+ exchange.targetProbes.totalMicroseconds + exchange.appliedAttacks.totalMicroseconds
				+ exchange.activationStart.totalMicroseconds + exchange.activationEnd.totalMicroseconds
				+ exchange.roundAdvance.totalMicroseconds
				+ exchange.remainderMicroseconds, exchange.body.totalMicroseconds);
		}
	}

	void forecastBestTarget(const std::shared_ptr<HypotheticBattle> & model,
		BattleBestTargetTiming * timing, std::optional<EvaluationResult> & result,
		const battle::Unit * selectedActor = nullptr)
	{
		const auto * actor = model->battleGetUnitByID(selectedActor ? selectedActor->unitId() : ordinary->unitId());
		ASSERT_NE(actor, nullptr);
		DamageCache cache;
		cache.buildDamageCache(model, BattleSide::ATTACKER);
		PotentialTargets targets(actor, cache, model);
		ASSERT_FALSE(targets.possibleAttacks.empty());
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, 1.0f, 2);
		result = evaluator.findBestTarget(actor, targets, cache, model, false, timing);
	}

	void expectExchangeClosure(const BattleMovementTiming & timing) const
	{
		const auto & exchange = timing.findMoveExchangeDetails;
		expectLifecycleSubsets(exchange);
		const uint64_t classified = exchange.membership.totalMicroseconds
			+ exchange.projectionSetup.totalMicroseconds + exchange.targetProbes.totalMicroseconds
			+ exchange.appliedAttacks.totalMicroseconds + exchange.activationStart.totalMicroseconds
			+ exchange.activationEnd.totalMicroseconds + exchange.roundAdvance.totalMicroseconds;
		EXPECT_LE(classified, exchange.body.totalMicroseconds);
		EXPECT_EQ(classified + exchange.remainderMicroseconds, exchange.body.totalMicroseconds);
		for(const auto * stage : {&exchange.membership, &exchange.projectionSetup,
			&exchange.targetProbes, &exchange.appliedAttacks, &exchange.activationStart,
			&exchange.activationEnd, &exchange.roundAdvance})
			EXPECT_LE(stage->totalMicroseconds, exchange.body.totalMicroseconds);
		EXPECT_LE(exchange.body.totalMicroseconds, timing.findMoveExchange.totalMicroseconds);
		EXPECT_LE(timing.findMoveExchange.totalMicroseconds, timing.findMove.totalMicroseconds);
		EXPECT_EQ(exchange.body.calls, timing.findMoveExchange.calls);
		EXPECT_LE(exchange.membership.calls, exchange.body.calls);
		EXPECT_LE(exchange.projectionSetup.calls, exchange.membership.calls);
	}

	void forecastMove(const std::shared_ptr<HypotheticBattle> & model,
		BattleMovementTiming * timing, MoveTarget & result)
	{
		const auto * actor = model->battleGetUnitByID(ordinary->unitId());
		ASSERT_NE(actor, nullptr);
		DamageCache cache;
		cache.buildDamageCache(model, BattleSide::ATTACKER);
		PotentialTargets targets(actor, cache, model);
		ASSERT_FALSE(targets.unreachableEnemies.empty());
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, 1.0f, 2);
		evaluator.updateReachabilityMap(model);
		result = evaluator.findMoveTowardsUnreachable(actor, targets, cache, model, timing);
	}
};

TEST_F(NewHorizonsExchangeTimingTest, BestTargetWaitAndNormalPartitionAndMatchNullSink)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	std::map<uint32_t, JsonNode> before;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		before.emplace(unit->unitId(), parent->getForUpdate(unit->unitId())->save());
	auto control = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto measured = std::make_shared<HypotheticBattle>(environment.get(), parent);
	for(const auto & [id, snapshot] : before)
	{
		control->getForUpdate(id);
		measured->getForUpdate(id);
	}
	std::optional<EvaluationResult> expectedResult;
	std::optional<EvaluationResult> actualResult;
	ASSERT_NO_FATAL_FAILURE(forecastBestTarget(control, nullptr, expectedResult));
	BattleBestTargetTiming timing;
	ASSERT_NO_FATAL_FAILURE(forecastBestTarget(measured, &timing, actualResult));
	ASSERT_TRUE(expectedResult.has_value());
	ASSERT_TRUE(actualResult.has_value());
	const auto & expected = *expectedResult;
	const auto & actual = *actualResult;
	EXPECT_EQ(std::bit_cast<uint32_t>(actual.score), std::bit_cast<uint32_t>(expected.score));
	EXPECT_EQ(actual.wait, expected.wait);
	EXPECT_EQ(actual.bestAttack.from, expected.bestAttack.from);
	EXPECT_EQ(actual.bestAttack.dest, expected.bestAttack.dest);
	EXPECT_EQ(actual.bestAttack.attack.attacker->unitId(), expected.bestAttack.attack.attacker->unitId());
	EXPECT_EQ(actual.bestAttack.attack.defender->unitId(), expected.bestAttack.attack.defender->unitId());
	ASSERT_GT(timing.waitedExchange.calls, 0u);
	EXPECT_EQ(timing.waitedExchange.calls, timing.normalExchange.calls);
	for(const auto * exchange : {&timing.waitedDetails, &timing.normalDetails})
	{
		EXPECT_GT(exchange->activationStart.calls, 0u);
		EXPECT_GT(exchange->activationEnd.calls, 0u);
		EXPECT_GT(exchange->roundAdvance.calls, 0u);
		EXPECT_GT(exchange->lifecycleSubsets.activationExpiryRemoval.calls, 0u);
		EXPECT_GT(exchange->lifecycleSubsets.activationGamblerRemoval.calls, 0u);
		EXPECT_GT(exchange->lifecycleSubsets.roundTimedUpdates.calls, 0u);
	}
	expectBestTargetClosure(timing);
	for(const auto & [id, snapshot] : before)
	{
		EXPECT_EQ(parent->getForUpdate(id)->save(), snapshot);
		EXPECT_EQ(control->getForUpdate(id)->save(), snapshot);
		EXPECT_EQ(measured->getForUpdate(id)->save(), snapshot);
		EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), snapshot);
	}
	EXPECT_TRUE(callback->actions.empty());
	EXPECT_TRUE(callback->heroActions.empty());
}

TEST_F(NewHorizonsExchangeTimingTest, BestTargetAlreadyWaitedSkipsWaitBuckets)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	model->makeWait(model->battleGetUnitByID(ordinary->unitId()));
	auto control = std::make_shared<HypotheticBattle>(environment.get(), model);
	std::optional<EvaluationResult> expected;
	ASSERT_NO_FATAL_FAILURE(forecastBestTarget(control, nullptr, expected));
	BattleBestTargetTiming timing;
	std::optional<EvaluationResult> result;
	ASSERT_NO_FATAL_FAILURE(forecastBestTarget(model, &timing, result));
	ASSERT_TRUE(expected.has_value());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(std::bit_cast<uint32_t>(result->score), std::bit_cast<uint32_t>(expected->score));
	EXPECT_EQ(result->wait, expected->wait);
	EXPECT_EQ(result->bestAttack.from, expected->bestAttack.from);
	EXPECT_EQ(result->bestAttack.dest, expected->bestAttack.dest);
	EXPECT_EQ(timing.waitedExchange.calls, 0u);
	EXPECT_EQ(timing.waitedExchange.totalMicroseconds, 0u);
	EXPECT_EQ(timing.waitedDetails.body.calls, 0u);
	EXPECT_EQ(timing.waitedDetails.remainderMicroseconds, 0u);
	for(const auto * stage : {&timing.waitedDetails.activationStart, &timing.waitedDetails.activationEnd,
		&timing.waitedDetails.roundAdvance})
	{
		EXPECT_EQ(stage->calls, 0u);
		EXPECT_EQ(stage->totalMicroseconds, 0u);
	}
	ASSERT_GT(timing.normalExchange.calls, 0u);
	for(const auto * stage : {&timing.waitedDetails.lifecycleSubsets.activationExpiryRemoval,
		&timing.waitedDetails.lifecycleSubsets.activationGamblerRemoval,
		&timing.waitedDetails.lifecycleSubsets.roundTimedUpdates})
	{
		EXPECT_EQ(stage->calls, 0u);
		EXPECT_EQ(stage->totalMicroseconds, 0u);
	}
	expectBestTargetClosure(timing);
}

TEST_F(NewHorizonsExchangeTimingTest, NonemptyNormalMovementPartitionsExchangeAndMatchesNullSink)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Real mixed participants exercise both future target probes and applied
	// resolution; every exchange retains Normal's two-round forecast.
	addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 3), 50);
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(9, 5), 50);
	addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 7), 50);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 3), 50);
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	std::map<uint32_t, JsonNode> parentBefore;
	std::map<uint32_t, JsonNode> liveBefore;
	for(const auto * unit : battle()->battleGetAllUnits(false))
	{
		const auto snapshot = unit->acquireState();
		liveBefore.emplace(unit->unitId(), snapshot->save());
		parentBefore.emplace(unit->unitId(), parent->getForUpdate(unit->unitId())->save());
	}
	auto control = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto measured = std::make_shared<HypotheticBattle>(environment.get(), parent);
	for(const auto & [id, snapshot] : parentBefore)
	{
		control->getForUpdate(id);
		measured->getForUpdate(id);
	}
	MoveTarget expected;
	ASSERT_NO_FATAL_FAILURE(forecastMove(control, nullptr, expected));
	BattleMovementTiming timing;
	MoveTarget actual;
	ASSERT_NO_FATAL_FAILURE(forecastMove(measured, &timing, actual));
	ASSERT_NE(actual.score, EvaluationResult::INEFFECTIVE_SCORE);
	ASSERT_TRUE(std::isfinite(actual.score));
	EXPECT_EQ(std::bit_cast<uint32_t>(actual.score), std::bit_cast<uint32_t>(expected.score));
	EXPECT_EQ(actual.positions.toVector(), expected.positions.toVector());
	EXPECT_EQ(actual.turnsToReach, expected.turnsToReach);
	ASSERT_EQ(actual.cachedAttack.has_value(), expected.cachedAttack.has_value());
	if(actual.cachedAttack)
	{
		EXPECT_EQ(actual.cachedAttack->attack.attacker->unitId(), expected.cachedAttack->attack.attacker->unitId());
		EXPECT_EQ(actual.cachedAttack->attack.defender->unitId(), expected.cachedAttack->attack.defender->unitId());
		EXPECT_EQ(actual.cachedAttack->attack.shooting, expected.cachedAttack->attack.shooting);
		EXPECT_EQ(actual.cachedAttack->from, expected.cachedAttack->from);
		EXPECT_EQ(actual.cachedAttack->dest, expected.cachedAttack->dest);
	}
	ASSERT_EQ(timing.findMove.calls, 1u);
	ASSERT_GT(timing.findMoveExchange.calls, 0u);
	ASSERT_GT(timing.findMoveExchangeDetails.projectionSetup.calls, 0u);
	ASSERT_GT(timing.findMoveExchangeDetails.targetProbes.calls, 0u);
	ASSERT_GT(timing.findMoveExchangeDetails.appliedAttacks.calls, 0u);
	EXPECT_GT(timing.findMoveExchangeDetails.activationStart.calls, 0u);
	EXPECT_GT(timing.findMoveExchangeDetails.activationEnd.calls, 0u);
	EXPECT_GT(timing.findMoveExchangeDetails.roundAdvance.calls, 0u);
	EXPECT_GT(timing.findMoveExchangeDetails.lifecycleSubsets.activationExpiryRemoval.calls, 0u);
	EXPECT_GT(timing.findMoveExchangeDetails.lifecycleSubsets.activationGamblerRemoval.calls, 0u);
	EXPECT_GT(timing.findMoveExchangeDetails.lifecycleSubsets.roundTimedUpdates.calls, 0u);
	EXPECT_EQ(timing.findMoveExchangeDetails.membership.calls, timing.findMoveExchangeDetails.body.calls);
	EXPECT_EQ(timing.findMoveExchangeDetails.projectionSetup.calls, timing.findMoveExchangeDetails.body.calls);
	expectExchangeClosure(timing);
	for(const auto & [id, snapshot] : parentBefore)
	{
		EXPECT_EQ(parent->getForUpdate(id)->save(), snapshot);
		EXPECT_EQ(control->getForUpdate(id)->save(), snapshot);
		EXPECT_EQ(measured->getForUpdate(id)->save(), snapshot);
	}
	for(const auto & [id, snapshot] : liveBefore)
	{
		const auto current = battle()->battleGetUnitByID(id)->acquireState();
		EXPECT_EQ(current->save(), snapshot);
	}
	EXPECT_TRUE(callback->actions.empty());
	EXPECT_TRUE(callback->heroActions.empty());
}

TEST_F(NewHorizonsExchangeTimingTest, AdjacentTargetsReturnBeforeExchangeAndKeepBucketsZero)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	PotentialTargets targets(ordinary, cache, model);
	ASSERT_TRUE(targets.unreachableEnemies.empty());
	BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, 1.0f, 2);
	const auto expected = evaluator.findMoveTowardsUnreachable(ordinary, targets, cache, model, nullptr);
	BattleMovementTiming timing;
	const auto actual = evaluator.findMoveTowardsUnreachable(ordinary, targets, cache, model, &timing);
	EXPECT_EQ(std::bit_cast<uint32_t>(actual.score), std::bit_cast<uint32_t>(expected.score));
	EXPECT_EQ(actual.positions.toVector(), expected.positions.toVector());
	EXPECT_EQ(actual.turnsToReach, expected.turnsToReach);
	EXPECT_FALSE(actual.cachedAttack.has_value());
	EXPECT_FALSE(expected.cachedAttack.has_value());
	EXPECT_TRUE(actual.positions.empty());
	EXPECT_EQ(timing.goTowards.calls, 0u);
	EXPECT_EQ(timing.findMove.calls, 1u);
	EXPECT_EQ(timing.findMoveExchange.calls, 0u);
	const auto & exchange = timing.findMoveExchangeDetails;
	for(const auto * stage : {&exchange.body, &exchange.membership, &exchange.projectionSetup,
		&exchange.targetProbes, &exchange.appliedAttacks, &exchange.activationStart,
		&exchange.activationEnd, &exchange.roundAdvance})
	{
		EXPECT_EQ(stage->calls, 0u);
		EXPECT_EQ(stage->totalMicroseconds, 0u);
	}
	EXPECT_EQ(exchange.remainderMicroseconds, 0u);
	for(const auto * stage : {&exchange.lifecycleSubsets.activationExpiryRemoval,
		&exchange.lifecycleSubsets.activationGamblerRemoval, &exchange.lifecycleSubsets.roundTimedUpdates})
	{
		EXPECT_EQ(stage->calls, 0u);
		EXPECT_EQ(stage->totalMicroseconds, 0u);
	}
	expectExchangeClosure(timing);
	EXPECT_TRUE(callback->actions.empty());
}

TEST_F(NewHorizonsExchangeTimingTest, ThrowingSubmissionRetainsRealExchangeCountsAndClosedNestedSummary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ai->autobattlePreferences.enableSpellsUsage = false;
	callback->onSubmit = [] { throw std::runtime_error("Expected exchange diagnostic submission failure"); };
	EXPECT_THROW(ai->activeStack(BattleID(0), ordinary), std::runtime_error);
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_EQ(callback->retreatDecisionCalls, 1u);
	const auto timing = summary();
	EXPECT_EQ(timing.activeStackInFlight, 0u);
	ASSERT_GT(timing.movementFindMoveExchangeSubset.calls, 0u);
	EXPECT_EQ(timing.movementExchangeBody.calls, timing.movementFindMoveExchangeSubset.calls);
	ASSERT_GT(timing.movementExchangeProjectionSetup.calls, 0u);
	const uint64_t classified = timing.movementExchangeMembership.totalMicroseconds
		+ timing.movementExchangeProjectionSetup.totalMicroseconds
		+ timing.movementExchangeTargetProbes.totalMicroseconds
		+ timing.movementExchangeAppliedAttacks.totalMicroseconds
		+ timing.movementExchangeActivationStart.totalMicroseconds
		+ timing.movementExchangeActivationEnd.totalMicroseconds
		+ timing.movementExchangeRoundAdvance.totalMicroseconds;
	EXPECT_LE(classified, timing.movementExchangeBody.totalMicroseconds);
	EXPECT_EQ(classified + timing.movementExchangeRemainderMicroseconds,
		timing.movementExchangeBody.totalMicroseconds);
	for(const auto * stage : {&timing.movementExchangeMembership, &timing.movementExchangeProjectionSetup,
		&timing.movementExchangeTargetProbes, &timing.movementExchangeAppliedAttacks,
		&timing.movementExchangeActivationStart, &timing.movementExchangeActivationEnd,
		&timing.movementExchangeRoundAdvance})
		EXPECT_LE(stage->totalMicroseconds, timing.movementExchangeBody.totalMicroseconds);
	EXPECT_LE(timing.movementExchangeBody.totalMicroseconds, timing.movementFindMoveExchangeSubset.totalMicroseconds);
	EXPECT_LE(timing.movementFindMoveExchangeSubset.totalMicroseconds, timing.movementFindMove.totalMicroseconds);
	EXPECT_EQ(timing.directUnitSubmission.calls, 1u);
	expectStageClosure(timing);
}

class NewHorizonsDetachedBonusRemovalTimingTest : public NewHorizonsBattleAITimingTest
{
protected:
	static Bonus genericRecord(int value = 103, SpellID sid = SpellID::HASTE)
	{
		return Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, value, BonusSourceID(sid));
	}

	static TConstBonusListPtr records(const StackWithBonuses & unit)
	{
		return unit.getUnstackedBonuses(CSelector([](const Bonus * bonus)
		{
			return bonus->type == BonusType::STACKS_SPEED && bonus->duration == BonusDuration::ONE_BATTLE
				&& (bonus->val == 103 || bonus->val == 104);
		}));
	}

	static std::vector<JsonNode> values(const TConstBonusListPtr & bonuses)
	{
		std::vector<JsonNode> result;
		for(const auto & bonus : *bonuses)
			result.push_back(bonus->toJsonNode());
		return result;
	}

	static std::string firstOrderedMismatch(const std::vector<JsonNode> & actual,
		const std::vector<JsonNode> & expected)
	{
		std::string result = "actual size=" + std::to_string(actual.size())
			+ ", expected size=" + std::to_string(expected.size());
		const auto common = std::min(actual.size(), expected.size());
		size_t index = 0;
		while(index < common && actual[index] == expected[index])
			++index;
		if(index == common && actual.size() == expected.size())
			return result + "; equal";
		return result + "; first mismatch index=" + std::to_string(index)
			+ "; actual=" + (index < actual.size() ? actual[index].toString() : "<absent>")
			+ "; expected=" + (index < expected.size() ? expected[index].toString() : "<absent>");
	}

	static void expectSameBonusMultiset(const std::vector<JsonNode> & actual,
		const std::vector<JsonNode> & expected)
	{
		ASSERT_EQ(actual.size(), expected.size());
		std::vector<bool> matched(expected.size(), false);
		for(const auto & bonus : actual)
		{
			size_t index = 0;
			while(index < expected.size() && (matched[index] || bonus != expected[index]))
				++index;
			ASSERT_LT(index, expected.size()) << "Unmatched full bonus metadata: " << bonus.toString();
			matched[index] = true;
		}
	}

	void expectGamblerRemovalParity(bool withPenalty, bool timeStopped)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto shared = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE)));
		shared->turnsRemain = 2;
		ordinary->addNewBonus(shared);
		attackerSideHero->addNewBonus(shared);
		const auto liveState = ordinary->acquireState()->save();
		auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		const auto original = parent->getForUpdate(ordinary->unitId());
		const auto penalty = newHorizonsCombatSkills::gamblerLuckPenalty();
		std::vector<Bonus> effects;
		if(withPenalty)
			effects.push_back(penalty);
		// Each predicate field matters; a different source ID alone does not.
		for(unsigned field = 0; field < 5; ++field)
		{
			auto nearMiss = penalty;
			switch(field)
			{
				case 0: nearMiss.duration = BonusDuration::ONE_BATTLE; break;
				case 1: nearMiss.type = BonusType::STACKS_SPEED; break;
				case 2: nearMiss.source = BonusSource::OTHER; break;
				case 3: ++nearMiss.val; break;
				case 4: nearMiss.stacking.clear(); break;
			}
			ASSERT_FALSE(newHorizonsCombatSkills::isGamblerLuckPenalty(&nearMiss));
			effects.push_back(nearMiss);
		}
		effects.emplace_back(BonusDuration::STACK_GETS_TURN, BonusType::STACKS_SPEED,
			BonusSource::OTHER, 7, BonusSourceID()); // Ordinary expiry is not Gambler removal.
		const SpellID confusion(SpellID::decode("new-horizons:confusion"));
		ASSERT_GE(confusion.getNum(), 0);
		effects.push_back(newHorizonsConfusionControl::pendingMarker(confusion, PlayerColor(1), false));
		Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
			BonusSource::SPELL_EFFECT, 17, BonusSourceID(SpellID(SpellID::HASTE)));
		guardian.turnsRemain = 2;
		effects.push_back(guardian);
		original->addUnitBonus(effects);
		if(timeStopped)
		{
			const SpellID stop(SpellID::decode("new-horizons:timeStop"));
			ASSERT_GE(stop.getNum(), 0);
			original->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
				BonusSource::SPELL_EFFECT, 0, BonusSourceID(stop))});
		}
		ASSERT_EQ(original->isTimeStopped(), timeStopped);
		ASSERT_EQ(original->guardianSpiritHitPoints, 17);
		ASSERT_EQ(original->guardianSpiritRoundsRemaining, 2);
		ASSERT_TRUE(original->hasBonusOfType(BonusType::CONFUSION_PENDING));
		const CSelector all([](const Bonus *) { return true; });
		const CSelector gambler(newHorizonsCombatSkills::isGamblerLuckPenalty);
		const auto stacked = original->getAllBonuses(all);
		const auto raw = original->getUnstackedBonuses(all);
		const auto beforeStacked = values(stacked);
		const auto beforeRaw = values(raw);
		const auto state = original->save();
		const auto parentVersion = original->getTreeVersion();
		auto generic = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto dedicated = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto control = generic->getForUpdate(ordinary->unitId());
		const auto actor = dedicated->getForUpdate(ordinary->unitId());
		const auto untouched = sibling->getForUpdate(ordinary->unitId());
		const auto siblingVersion = untouched->getTreeVersion();
		const auto retainedStacked = actor->getAllBonuses(all);
		const auto retainedRaw = actor->getUnstackedBonuses(all);
		const auto actorBeforeStacked = values(retainedStacked);
		const auto actorBeforeRaw = values(retainedRaw);
		const auto controlBeforeStacked = values(control->getAllBonuses(all));
		const auto controlRaw = control->getUnstackedBonuses(all);
		const auto controlBeforeRaw = values(controlRaw);
		const auto siblingBeforeStacked = values(untouched->getAllBonuses(all));
		const auto siblingBeforeRaw = values(untouched->getUnstackedBonuses(all));
		// Constructor capture can relocate spell records relative to local ADDs.
		// Cross-model preservation is full metadata plus multiplicity; ordering
		// remains strict within each model and between the two equivalent paths.
		for(const auto & snapshot : {actorBeforeStacked, controlBeforeStacked, siblingBeforeStacked})
		{
			ASSERT_NO_FATAL_FAILURE(expectSameBonusMultiset(snapshot, beforeStacked));
		}
		for(const auto & snapshot : {actorBeforeRaw, controlBeforeRaw, siblingBeforeRaw})
		{
			ASSERT_NO_FATAL_FAILURE(expectSameBonusMultiset(snapshot, beforeRaw));
		}
		ASSERT_EQ(actorBeforeStacked, controlBeforeStacked);
		ASSERT_EQ(actorBeforeRaw, controlBeforeRaw);
		std::vector<JsonNode> expectedRaw;
		for(const auto & bonus : *controlRaw)
			if(!gambler(bonus.get()))
				expectedRaw.push_back(bonus->toJsonNode());
		RecordProperty("preRemovalChildStackedVersusOriginal",
			firstOrderedMismatch(values(retainedStacked), beforeStacked));
		RecordProperty("preRemovalChildRawVersusOriginal",
			firstOrderedMismatch(values(retainedRaw), beforeRaw));
		ASSERT_EQ(actor->getUnstackedBonuses(gambler)->size(), withPenalty ? 1u : 0u);
		const auto controlVersion = control->getTreeVersion();
		const auto actorVersion = actor->getTreeVersion();
		control->removeUnitBonus(gambler);
		actor->removeGamblerLuckPenalty();
		EXPECT_EQ(control->getTreeVersion(), controlVersion + 1);
		EXPECT_EQ(actor->getTreeVersion(), actorVersion + 1);
		EXPECT_TRUE(actor->getUnstackedBonuses(gambler)->empty());
		EXPECT_TRUE(control->getUnstackedBonuses(gambler)->empty());
		const auto afterStacked = values(control->getAllBonuses(all));
		const auto afterRaw = values(control->getUnstackedBonuses(all));
		EXPECT_EQ(afterRaw, expectedRaw) << firstOrderedMismatch(afterRaw, expectedRaw);
		EXPECT_EQ(values(actor->getAllBonuses(all)), afterStacked);
		EXPECT_EQ(values(actor->getUnstackedBonuses(all)), afterRaw);
		EXPECT_EQ(controlBeforeRaw.size() - afterRaw.size(), withPenalty ? 1u : 0u);
		for(const auto & unit : {control, actor})
		{
			EXPECT_EQ(unit->save(), state);
			EXPECT_EQ(unit->guardianSpiritHitPoints, 17);
			EXPECT_EQ(unit->guardianSpiritRoundsRemaining, 2);
			EXPECT_TRUE(unit->hasBonusOfType(BonusType::CONFUSION_PENDING));
			EXPECT_EQ(unit->isTimeStopped(), timeStopped);
			const auto aliases = unit->getUnstackedBonuses(CSelector([](const Bonus * bonus)
			{
				return bonus->source == BonusSource::SPELL_EFFECT && bonus->type == BonusType::STACKS_SPEED
					&& bonus->sid == BonusSourceID(SpellID(SpellID::HASTE));
			}));
			ASSERT_EQ(aliases->size(), 2u);
			EXPECT_EQ((*aliases)[0].get(), (*aliases)[1].get());
			EXPECT_EQ((*aliases)[0]->turnsRemain, 2);
		}
		// Nested no-match removal still captures and invalidates once. Neither
		// parent, sibling, nor retained older query owners may be mutated.
		auto nestedGeneric = std::make_shared<HypotheticBattle>(environment.get(), generic);
		auto nestedDedicated = std::make_shared<HypotheticBattle>(environment.get(), dedicated);
		const auto nestedControl = nestedGeneric->getForUpdate(ordinary->unitId());
		const auto nestedActor = nestedDedicated->getForUpdate(ordinary->unitId());
		const auto nestedControlBeforeStacked = values(nestedControl->getAllBonuses(all));
		const auto nestedControlBeforeRaw = values(nestedControl->getUnstackedBonuses(all));
		const auto nestedActorBeforeStacked = values(nestedActor->getAllBonuses(all));
		const auto nestedActorBeforeRaw = values(nestedActor->getUnstackedBonuses(all));
		for(const auto & snapshot : {nestedControlBeforeStacked, nestedActorBeforeStacked})
		{
			ASSERT_NO_FATAL_FAILURE(expectSameBonusMultiset(snapshot, afterStacked));
			ASSERT_EQ(snapshot, afterStacked) << firstOrderedMismatch(snapshot, afterStacked);
		}
		for(const auto & snapshot : {nestedControlBeforeRaw, nestedActorBeforeRaw})
		{
			ASSERT_NO_FATAL_FAILURE(expectSameBonusMultiset(snapshot, afterRaw));
			ASSERT_EQ(snapshot, afterRaw) << firstOrderedMismatch(snapshot, afterRaw);
		}
		const auto nestedControlVersion = nestedControl->getTreeVersion();
		const auto nestedActorVersion = nestedActor->getTreeVersion();
		nestedControl->removeUnitBonus(gambler);
		nestedActor->removeGamblerLuckPenalty();
		EXPECT_EQ(nestedControl->getTreeVersion(), nestedControlVersion + 1);
		EXPECT_EQ(nestedActor->getTreeVersion(), nestedActorVersion + 1);
		for(const auto & unit : {control, actor, nestedControl, nestedActor})
		{
			EXPECT_EQ(values(unit->getAllBonuses(all)), afterStacked);
			EXPECT_EQ(values(unit->getUnstackedBonuses(all)), afterRaw);
			EXPECT_EQ(unit->save(), state);
		}
		for(const auto & unit : {original, untouched})
		{
			const auto currentStacked = values(unit->getAllBonuses(all));
			const auto currentRaw = values(unit->getUnstackedBonuses(all));
			const auto role = unit == original ? "original" : "untouched sibling";
			const auto & expectedStacked = unit == original ? beforeStacked : siblingBeforeStacked;
			const auto & expectedRawView = unit == original ? beforeRaw : siblingBeforeRaw;
			EXPECT_EQ(currentStacked, expectedStacked)
				<< role << ": " << firstOrderedMismatch(currentStacked, expectedStacked);
			EXPECT_EQ(currentRaw, expectedRawView)
				<< role << ": " << firstOrderedMismatch(currentRaw, expectedRawView);
			EXPECT_EQ(unit->save(), state);
		}
		EXPECT_EQ(original->getTreeVersion(), parentVersion);
		EXPECT_EQ(untouched->getTreeVersion(), siblingVersion);
		EXPECT_EQ(values(retainedStacked), actorBeforeStacked)
			<< firstOrderedMismatch(values(retainedStacked), actorBeforeStacked);
		EXPECT_EQ(values(retainedRaw), actorBeforeRaw)
			<< firstOrderedMismatch(values(retainedRaw), actorBeforeRaw);
		EXPECT_EQ(values(controlRaw), controlBeforeRaw);
		EXPECT_EQ(values(stacked), beforeStacked);
		EXPECT_EQ(values(raw), beforeRaw);
		EXPECT_EQ(shared->turnsRemain, 2);
		EXPECT_EQ(ordinary->acquireState()->save(), liveState);
		EXPECT_TRUE(callback->actions.empty());
		EXPECT_TRUE(callback->heroActions.empty());
	}
};

TEST_F(NewHorizonsDetachedBonusRemovalTimingTest, DedicatedGamblerRemovalMatchesGenericWithControlStateAndRawAliases)
{
	ASSERT_NO_FATAL_FAILURE(expectGamblerRemovalParity(true, false));
}

TEST_F(NewHorizonsDetachedBonusRemovalTimingTest, DedicatedGamblerNoMatchMatchesGenericWhileTimeStopped)
{
	ASSERT_NO_FATAL_FAILURE(expectGamblerRemovalParity(false, true));
}

TEST_F(NewHorizonsDetachedBonusRemovalTimingTest, NoMatchAndRealRemovalKeepOrderedBonusesStateAndBranchOwnership)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto liveBefore = ordinary->acquireState()->save();
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto actor = parent->getForUpdate(ordinary->unitId());
	actor->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, 3, BonusSourceID()), Bonus(BonusDuration::ONE_BATTLE,
		BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID())});
	const CSelector all([](const Bonus *) { return true; });
	const CSelector removed([](const Bonus * bonus)
	{
		return bonus->source == BonusSource::OTHER && bonus->type == BonusType::STACKS_SPEED && bonus->val == 3;
	});
	const auto ordered = [&all](const StackWithBonuses & unit)
	{
		std::vector<JsonNode> result;
		const auto bonuses = unit.getAllBonuses(all);
		for(const auto & bonus : *bonuses)
			result.push_back(bonus->toJsonNode());
		return result;
	};
	const auto before = ordered(*actor);
	std::vector<JsonNode> afterRemoval;
	unsigned matching = 0;
	const auto originalBonuses = actor->getAllBonuses(all);
	for(const auto & bonus : *originalBonuses)
	{
		if(removed(bonus.get()))
			++matching;
		else
			afterRemoval.push_back(bonus->toJsonNode());
	}
	ASSERT_EQ(matching, 1u);
	const auto state = actor->save();
	const auto parentVersion = actor->getTreeVersion();
	auto noMatch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto real = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto noMatchActor = noMatch->getForUpdate(actor->unitId());
	const auto realActor = real->getForUpdate(actor->unitId());
	ASSERT_EQ(ordered(*noMatchActor), before);
	ASSERT_EQ(ordered(*realActor), before);
	const auto noMatchVersion = noMatchActor->getTreeVersion();
	const auto realVersion = realActor->getTreeVersion();
	const auto measure = [](const std::function<void()> & action)
	{
		const auto start = std::chrono::steady_clock::now();
		action();
		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
	};
	RecordProperty("noMatchRemovalUs", std::to_string(measure([&]
	{
		noMatchActor->removeUnitBonus(CSelector([](const Bonus *) { return false; }));
	})));
	EXPECT_EQ(ordered(*noMatchActor), before);
	EXPECT_EQ(ordered(*realActor), before);
	EXPECT_EQ(noMatchActor->save(), state);
	// Current API invalidates even a no-match removal. Do not optimize it away
	// merely because the ordered effective bonuses and unit state are unchanged.
	EXPECT_EQ(noMatchActor->getTreeVersion(), noMatchVersion + 1);
	RecordProperty("realRemovalUs", std::to_string(measure([&] { realActor->removeUnitBonus(removed); })));
	EXPECT_EQ(ordered(*realActor), afterRemoval);
	EXPECT_EQ(realActor->save(), state);
	EXPECT_EQ(realActor->getTreeVersion(), realVersion + 1);
	EXPECT_EQ(ordered(*noMatchActor), before);
	EXPECT_EQ(noMatchActor->save(), state);
	EXPECT_EQ(ordered(*actor), before);
	EXPECT_EQ(actor->save(), state);
	EXPECT_EQ(actor->getTreeVersion(), parentVersion);
	EXPECT_EQ(battle()->battleGetUnitByID(actor->unitId())->acquireState()->save(), liveBefore);
	EXPECT_TRUE(callback->actions.empty());
	EXPECT_TRUE(callback->heroActions.empty());
}

TEST_F(NewHorizonsDetachedBonusRemovalTimingTest, DistinctEqualAddsKeepIdentityAndRemovalPreservesSourceSidValueDifferences)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto original = parent->getForUpdate(ordinary->unitId());
	Bonus otherSource = genericRecord();
	otherSource.source = BonusSource::CREATURE_ABILITY;
	otherSource.sid = BonusSourceID(creatureByName("core:pikeman"));
	original->addUnitBonus({genericRecord(), genericRecord(), genericRecord(104),
		genericRecord(103, SpellID::SLOW), otherSource});
	const auto before = records(*original);
	ASSERT_EQ(before->size(), 5u);
	EXPECT_EQ((*before)[0]->toJsonNode(), (*before)[1]->toJsonNode());
	ASSERT_NE((*before)[0].get(), (*before)[1].get());
	const auto repeated = records(*original);
	for(size_t index = 0; index < before->size(); ++index)
		EXPECT_EQ((*repeated)[index].get(), (*before)[index].get());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto removed = child->getForUpdate(original->unitId());
	const auto untouched = sibling->getForUpdate(original->unitId());
	const auto state = original->save();
	const auto pointer = (*before)[0].get();
	removed->removeUnitBonus(CSelector([pointer](const Bonus * bonus) { return bonus == pointer; }));
	const auto remaining = records(*removed);
	ASSERT_EQ(remaining->size(), 4u);
	for(size_t index = 0; index < remaining->size(); ++index)
	{
		EXPECT_EQ((*remaining)[index].get(), (*before)[index + 1].get());
		EXPECT_EQ((*remaining)[index]->toJsonNode(), (*before)[index + 1]->toJsonNode());
	}
	EXPECT_EQ(values(records(*original)), values(before));
	EXPECT_EQ(values(records(*untouched)), values(before));
	EXPECT_EQ(original->save(), state);
	EXPECT_EQ(removed->save(), state);
	EXPECT_EQ(untouched->save(), state);
}

TEST_F(NewHorizonsDetachedBonusRemovalTimingTest, NestedRemovalAndLaterEqualChildAddDoNotBecomeValueTombstones)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto original = parent->getForUpdate(ordinary->unitId());
	original->addUnitBonus({genericRecord()});
	const auto state = original->save();
	const auto inherited = records(*original);
	ASSERT_EQ(inherited->size(), 1u);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto childActor = child->getForUpdate(original->unitId());
	const auto first = inherited->front().get();
	childActor->removeUnitBonus(CSelector([first](const Bonus * bonus) { return bonus == first; }));
	ASSERT_TRUE(records(*childActor)->empty());
	childActor->addUnitBonus({genericRecord()});
	const auto later = records(*childActor);
	ASSERT_EQ(later->size(), 1u);
	ASSERT_NE(later->front().get(), first);
	EXPECT_EQ(later->front()->toJsonNode(), inherited->front()->toJsonNode());
	auto grandchild = std::make_shared<HypotheticBattle>(environment.get(), child);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), child);
	const auto grandActor = grandchild->getForUpdate(original->unitId());
	const auto siblingActor = sibling->getForUpdate(original->unitId());
	const auto grandRecords = records(*grandActor);
	ASSERT_EQ(grandRecords->size(), 1u);
	ASSERT_EQ(grandRecords->front().get(), later->front().get());
	const auto second = later->front().get();
	grandActor->removeUnitBonus(CSelector([second](const Bonus * bonus) { return bonus == second; }));
	EXPECT_TRUE(records(*grandActor)->empty());
	EXPECT_EQ(values(records(*childActor)), values(later));
	EXPECT_EQ(values(records(*siblingActor)), values(later));
	EXPECT_EQ(values(records(*original)), values(inherited));
	for(const auto & unit : {original, childActor, grandActor, siblingActor})
		EXPECT_EQ(unit->save(), state);
}

TEST_F(NewHorizonsDetachedBonusRemovalTimingTest, GenericUpdateFallbackHasStableIdentityAcrossQueriesAndNestedRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto original = parent->getForUpdate(ordinary->unitId());
	original->updateUnitBonus({genericRecord(), genericRecord(104, SpellID::SLOW)});
	const auto state = original->save();
	const auto before = records(*original);
	ASSERT_EQ(before->size(), 2u);
	const auto repeated = records(*original);
	for(size_t index = 0; index < before->size(); ++index)
		EXPECT_EQ((*repeated)[index].get(), (*before)[index].get());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto childActor = child->getForUpdate(original->unitId());
	const auto first = before->front().get();
	childActor->removeUnitBonus(CSelector([first](const Bonus * bonus) { return bonus == first; }));
	const auto kept = records(*childActor);
	ASSERT_EQ(kept->size(), 1u);
	EXPECT_EQ(kept->front().get(), (*before)[1].get());
	childActor->updateUnitBonus({genericRecord()});
	const auto restored = records(*childActor);
	ASSERT_EQ(restored->size(), 2u);
	EXPECT_EQ(restored->front().get(), (*before)[1].get());
	ASSERT_NE((*restored)[1].get(), first);
	auto grandchild = std::make_shared<HypotheticBattle>(environment.get(), child);
	const auto grandActor = grandchild->getForUpdate(original->unitId());
	const auto second = (*before)[1].get();
	grandActor->removeUnitBonus(CSelector([second](const Bonus * bonus) { return bonus == second; }));
	ASSERT_EQ(records(*grandActor)->size(), 1u);
	EXPECT_EQ(records(*grandActor)->front().get(), (*restored)[1].get());
	EXPECT_EQ(values(records(*original)), values(before));
	EXPECT_EQ(values(records(*childActor)), values(restored));
	for(const auto & unit : {original, childActor, grandActor})
		EXPECT_EQ(unit->save(), state);
}

TEST_F(NewHorizonsDetachedBonusRemovalTimingTest, RepeatedCaptureKeepsRetainedSpellAndMarkedGroupViewsIndependent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto shared = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE)));
	shared->turnsRemain = 2;
	ordinary->addNewBonus(shared);
	attackerSideHero->addNewBonus(shared); // One source identity in two raw DAG paths.
	const BonusSourceID group(BonusCustomSource(1701));
	ordinary->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -1, group));
	Bonus marker(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION, BonusSource::OTHER, 0, group);
	JsonNode metadata;
	metadata["kind"].String() = "bleeding";
	metadata["applicationOrder"].Integer() = 1;
	marker.parameters = std::make_shared<BonusParameters>(metadata);
	ordinary->addNewBonus(std::make_shared<Bonus>(marker));
	const CSelector selected([group](const Bonus * bonus)
	{
		return (bonus->source == BonusSource::SPELL_EFFECT && bonus->sid == BonusSourceID(SpellID(SpellID::HASTE)))
			|| (bonus->source == BonusSource::OTHER && bonus->sid == group);
	});
	const auto spell = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)));
	const auto liveState = ordinary->acquireState()->save();
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	const auto original = parent->getForUpdate(ordinary->unitId());
	const auto stacked = original->getAllBonuses(selected);
	const auto raw = original->getUnstackedBonuses(selected);
	ASSERT_EQ(stacked->size(), 3u);
	ASSERT_EQ(raw->size(), 4u);
	const auto stackedValues = values(stacked);
	const auto rawValues = values(raw);
	const auto parentVersion = original->getTreeVersion();
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto actor = child->getForUpdate(ordinary->unitId());
	const auto other = sibling->getForUpdate(ordinary->unitId());
	const auto childVersion = actor->getTreeVersion();
	const auto siblingVersion = other->getTreeVersion();
	std::vector<TConstBonusListPtr> retainedStacked{actor->getAllBonuses(selected)};
	std::vector<TConstBonusListPtr> retainedRaw{actor->getUnstackedBonuses(selected)};
	for(unsigned iteration = 1; iteration <= 3; ++iteration)
	{
		actor->removeUnitBonus(CSelector([](const Bonus *) { return false; }));
		retainedStacked.push_back(actor->getAllBonuses(selected));
		retainedRaw.push_back(actor->getUnstackedBonuses(selected));
		for(const auto & owner : retainedStacked)
			EXPECT_EQ(values(owner), stackedValues);
		for(const auto & owner : retainedRaw)
			EXPECT_EQ(values(owner), rawValues);
		const auto aliases = actor->getUnstackedBonuses(spell);
		ASSERT_EQ(aliases->size(), 2u);
		EXPECT_EQ((*aliases)[0].get(), (*aliases)[1].get());
		EXPECT_EQ((*aliases)[0]->turnsRemain, 2);
		EXPECT_EQ(actor->getTreeVersion(), childVersion + iteration);
		EXPECT_EQ(values(original->getAllBonuses(selected)), stackedValues);
		EXPECT_EQ(values(original->getUnstackedBonuses(selected)), rawValues);
		EXPECT_EQ(values(other->getAllBonuses(selected)), stackedValues);
		EXPECT_EQ(values(other->getUnstackedBonuses(selected)), rawValues);
		EXPECT_EQ(original->getTreeVersion(), parentVersion);
		EXPECT_EQ(other->getTreeVersion(), siblingVersion);
		EXPECT_EQ(actor->save(), liveState);
	}
	EXPECT_EQ(values(stacked), stackedValues);
	EXPECT_EQ(values(raw), rawValues);
	EXPECT_EQ(shared->turnsRemain, 2);
	EXPECT_EQ(ordinary->acquireState()->save(), liveState);
}

// Equal creature counts/HP, not equal action economy or geometry: splitting the
// army deliberately changes queue and target multiplicity. This measures native
// decision work, not GUI pacing, server latency, or Windows performance.
class NewHorizonsBattleDwarfWaitTimingTest : public NewHorizonsBattleAITimingTest
{
protected:
	void measureNaturalRound(unsigned stackCount, unsigned opposingStackCount = 1)
	{
		ASSERT_TRUE(stackCount == 1 || stackCount == 7);
		ASSERT_TRUE(opposingStackCount == 1 || opposingStackCount == 7);
		useCommands = false;
		startGame();
		gameState()->getStartInfo()->difficulty = static_cast<ui8>(EMapDifficulty::NORMAL);
		ASSERT_EQ(gameState()->getStartInfo()->getDifficulty(), EMapDifficulty::NORMAL);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		int64_t opposingHealth = 0;
		for(unsigned index = 0; index < opposingStackCount; ++index)
		{
			const auto * pikeman = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
				BattleHex(3, opposingStackCount == 1 ? 5 : index + 1), 70 / opposingStackCount);
			EXPECT_EQ(pikeman->getAvailableHealth(), static_cast<int64_t>(70 / opposingStackCount) * pikeman->getMaxHealth());
			opposingHealth += pikeman->getAvailableHealth();
		}
		int64_t initialHealth = 0;
		for(unsigned index = 0; index < stackCount; ++index)
		{
			const auto * dwarf = addStack(BattleSide::DEFENDER, creatureByName("core:battleDwarf"),
				BattleHex(13, stackCount == 1 ? 4 : index + 1), 70 / stackCount);
			initialHealth += dwarf->getAvailableHealth();
			EXPECT_EQ(dwarf->getAvailableHealth(), static_cast<int64_t>(70 / stackCount) * dwarf->getMaxHealth());
		}
		RecordProperty("initialDwarfHealth", std::to_string(initialHealth));
		ASSERT_EQ(initialHealth, 1400);
		RecordProperty("dwarfStacks", std::to_string(stackCount));
		RecordProperty("opposingStacks", std::to_string(opposingStackCount));
		RecordProperty("initialOpposingHealth", std::to_string(opposingHealth));
		RecordProperty("opposingCreatureCount", "70");
		RecordProperty("forecastRounds", "2"); // Normal production getSimulationTurnsCount.
		beginCombat();
		callback = std::make_shared<TimingCallback>(PlayerColor(1));
		callback->onBattleStarted(battle());
		environment = std::make_shared<TimingEnvironment>(gameState());
		ai = std::make_unique<CBattleAI>();
		ai->initBattleInterface(environment, callback);
		ai->battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::DEFENDER, false);
		ai->autobattlePreferences.enableSpellsUsage = false;
		const auto openingRound = battle()->getRound();
		unsigned decisions = 0;
		unsigned waits = 0;
		for(unsigned actionIndex = 0; actionIndex < 128; ++actionIndex)
		{
			ASSERT_NE(gameState()->getBattle(BattleID(0)), nullptr);
			if(battle()->getRound() != openingRound)
				break;
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			const auto controller = battle()->battleGetOwner(active);
			if(controller != PlayerColor(1))
			{
				ASSERT_EQ(controller, PlayerColor(0));
				ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), controller,
					BattleAction::makeDefend(active)));
				continue;
			}
			const auto * actor = battle()->getStack(active->unitId());
			ASSERT_NE(actor, nullptr);
			const auto before = summary();
			const auto serial = battle()->getActivationSerial();
			std::map<uint32_t, JsonNode> snapshots;
			for(const auto * unit : battle()->battleGetAllUnits(false))
				snapshots.emplace(unit->unitId(), unit->acquireState()->save());
			const auto submitted = callback->actions.size();
			ai->activeStack(BattleID(0), actor);
			const auto after = summary();
			ASSERT_EQ(callback->actions.size(), submitted + 1);
			ASSERT_TRUE(callback->heroActions.empty());
			ASSERT_NE(gameState()->getBattle(BattleID(0)), nullptr);
			EXPECT_EQ(battle()->getActivationSerial(), serial);
			for(const auto & [id, snapshot] : snapshots)
			{
				const auto * unit = battle()->battleGetUnitByID(id);
				ASSERT_NE(unit, nullptr);
				EXPECT_EQ(unit->acquireState()->save(), snapshot);
			}
			const auto action = callback->actions.back();
			ASSERT_EQ(action.stackNumber, actor->unitId());
			ASSERT_EQ(action.side, BattleSide::DEFENDER);
			const auto prefix = "decision" + std::to_string(decisions++) + "_";
			RecordProperty(prefix + "action", std::to_string(static_cast<int>(action.actionType)));
			RecordProperty(prefix + "wallUs", std::to_string(after.activeStackTotalMicroseconds - before.activeStackTotalMicroseconds));
			const auto stage = [&](const char * name, const auto & previous, const auto & current)
			{
				RecordProperty(prefix + name + "Us", std::to_string(current.totalMicroseconds - previous.totalMicroseconds));
				RecordProperty(prefix + name + "Calls", std::to_string(current.calls - previous.calls));
			};
			stage("construction", before.evaluatorConstruction, after.evaluatorConstruction);
			stage("selection", before.stackActionSelection, after.stackActionSelection);
			stage("creatureSpell", before.selectionCreatureSpell, after.selectionCreatureSpell);
			stage("movement", before.selectionMovementSearch, after.selectionMovementSearch);
			stage("movementBody", before.movementExchangeBody, after.movementExchangeBody);
			stage("movementProbes", before.movementExchangeTargetProbes, after.movementExchangeTargetProbes);
			stage("movementAppliedAttacks", before.movementExchangeAppliedAttacks, after.movementExchangeAppliedAttacks);
			stage("movementActivationStart", before.movementExchangeActivationStart, after.movementExchangeActivationStart);
			stage("movementActivationEnd", before.movementExchangeActivationEnd, after.movementExchangeActivationEnd);
			stage("movementRoundAdvance", before.movementExchangeRoundAdvance, after.movementExchangeRoundAdvance);
			stage("movementActivationExpiryRemoval", before.movementExchangeActivationExpiryRemoval, after.movementExchangeActivationExpiryRemoval);
			stage("movementActivationGamblerRemoval", before.movementExchangeActivationGamblerRemoval, after.movementExchangeActivationGamblerRemoval);
			stage("movementRoundTimedUpdates", before.movementExchangeRoundTimedUpdates, after.movementExchangeRoundTimedUpdates);
			stage("bestTarget", before.selectionBestTarget, after.selectionBestTarget);
			stage("waitedExchange", before.bestTargetWaitedExchange, after.bestTargetWaitedExchange);
			stage("normalExchange", before.bestTargetNormalExchange, after.bestTargetNormalExchange);
			stage("waitedBody", before.bestTargetWaitedBody, after.bestTargetWaitedBody);
			stage("normalBody", before.bestTargetNormalBody, after.bestTargetNormalBody);
			stage("waitedProbes", before.bestTargetWaitedTargetProbes, after.bestTargetWaitedTargetProbes);
			stage("normalProbes", before.bestTargetNormalTargetProbes, after.bestTargetNormalTargetProbes);
			stage("waitedActivationStart", before.bestTargetWaitedActivationStart, after.bestTargetWaitedActivationStart);
			stage("waitedActivationEnd", before.bestTargetWaitedActivationEnd, after.bestTargetWaitedActivationEnd);
			stage("waitedRoundAdvance", before.bestTargetWaitedRoundAdvance, after.bestTargetWaitedRoundAdvance);
			stage("normalActivationStart", before.bestTargetNormalActivationStart, after.bestTargetNormalActivationStart);
			stage("normalActivationEnd", before.bestTargetNormalActivationEnd, after.bestTargetNormalActivationEnd);
			stage("normalRoundAdvance", before.bestTargetNormalRoundAdvance, after.bestTargetNormalRoundAdvance);
			stage("waitedActivationExpiryRemoval", before.bestTargetWaitedActivationExpiryRemoval, after.bestTargetWaitedActivationExpiryRemoval);
			stage("waitedActivationGamblerRemoval", before.bestTargetWaitedActivationGamblerRemoval, after.bestTargetWaitedActivationGamblerRemoval);
			stage("waitedRoundTimedUpdates", before.bestTargetWaitedRoundTimedUpdates, after.bestTargetWaitedRoundTimedUpdates);
			stage("normalActivationExpiryRemoval", before.bestTargetNormalActivationExpiryRemoval, after.bestTargetNormalActivationExpiryRemoval);
			stage("normalActivationGamblerRemoval", before.bestTargetNormalActivationGamblerRemoval, after.bestTargetNormalActivationGamblerRemoval);
			stage("normalRoundTimedUpdates", before.bestTargetNormalRoundTimedUpdates, after.bestTargetNormalRoundTimedUpdates);
			stage("submission", before.directUnitSubmission, after.directUnitSubmission);
			const auto effectWork = [&](const char * context,
				const HypotheticLifecycleTiming::EffectWorkTiming & previous,
				const HypotheticLifecycleTiming::EffectWorkTiming & current)
			{
				for(const auto & [name, member] : {
					std::pair{"stackedQuery", &HypotheticLifecycleTiming::EffectWorkTiming::stackedQuery},
					std::pair{"rawQuery", &HypotheticLifecycleTiming::EffectWorkTiming::rawQuery},
					std::pair{"snapshotMaterialization", &HypotheticLifecycleTiming::EffectWorkTiming::snapshotMaterialization},
					std::pair{"selectiveRemovalQuery", &HypotheticLifecycleTiming::EffectWorkTiming::selectiveRemovalQuery},
					std::pair{"removalMutation", &HypotheticLifecycleTiming::EffectWorkTiming::removalMutation},
					std::pair{"roundAging", &HypotheticLifecycleTiming::EffectWorkTiming::roundAging},
				})
				{
					const std::string key = std::string(context) + "_" + name;
					stage(key.c_str(), previous.*member, current.*member);
				}
			};
			effectWork("movementActivationExpiry", before.movementExchangeActivationExpiryWork, after.movementExchangeActivationExpiryWork);
			effectWork("movementActivationGambler", before.movementExchangeActivationGamblerWork, after.movementExchangeActivationGamblerWork);
			effectWork("movementRoundTimed", before.movementExchangeRoundTimedWork, after.movementExchangeRoundTimedWork);
			effectWork("waitedActivationExpiry", before.bestTargetWaitedActivationExpiryWork, after.bestTargetWaitedActivationExpiryWork);
			effectWork("waitedActivationGambler", before.bestTargetWaitedActivationGamblerWork, after.bestTargetWaitedActivationGamblerWork);
			effectWork("waitedRoundTimed", before.bestTargetWaitedRoundTimedWork, after.bestTargetWaitedRoundTimedWork);
			effectWork("normalActivationExpiry", before.bestTargetNormalActivationExpiryWork, after.bestTargetNormalActivationExpiryWork);
			effectWork("normalActivationGambler", before.bestTargetNormalActivationGamblerWork, after.bestTargetNormalActivationGamblerWork);
			effectWork("normalRoundTimed", before.bestTargetNormalRoundTimedWork, after.bestTargetNormalRoundTimedWork);
			RecordProperty(prefix + "bestTargetRemainderUs", std::to_string(after.bestTargetRemainderMicroseconds - before.bestTargetRemainderMicroseconds));
			expectStageClosure(after);
			EXPECT_EQ(after.activeStackCalls - before.activeStackCalls, 1u);
			EXPECT_EQ(after.evaluatorConstruction.calls - before.evaluatorConstruction.calls, 1u);
			if(action.actionType == EActionType::WAIT)
				++waits;
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), controller, action));
		}
		ASSERT_NE(gameState()->getBattle(BattleID(0)), nullptr);
		EXPECT_GT(battle()->getRound(), openingRound) << "Natural round exceeded bounded action budget";
		EXPECT_GE(decisions, stackCount);
		RecordProperty("decisions", std::to_string(decisions));
		RecordProperty("naturalWaits", std::to_string(waits));
	}
};

TEST_F(NewHorizonsBattleDwarfWaitTimingTest, OneStackNormalTwoRoundDecision)
{
	ASSERT_NO_FATAL_FAILURE(measureNaturalRound(1));
}

TEST_F(NewHorizonsBattleDwarfWaitTimingTest, SevenStacksNormalTwoRoundDecisions)
{
	ASSERT_NO_FATAL_FAILURE(measureNaturalRound(7));
}

TEST_F(NewHorizonsBattleDwarfWaitTimingTest, SevenStacksAgainstSevenOpposingStacksNormalTwoRoundDecisions)
{
	// Same seventy creatures per side as the existing seven-versus-one case;
	// only opposing stack multiplicity and corresponding legal start hexes vary.
	// WAIT is observed, never forced or required by this timing fixture.
	ASSERT_NO_FATAL_FAILURE(measureNaturalRound(7, 7));
}
