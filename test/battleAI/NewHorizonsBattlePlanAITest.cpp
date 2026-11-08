/*
 * NewHorizonsBattlePlanAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../AI/BattleAI/HeroDecisionPressure.h"
#include "../../AI/BattleAI/HeroOrderCandidates.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/GameConstants.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto battlePlanPerk = "new-horizons:command.battlePlan";

class BattlePlanAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BattlePlanAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class RecordingBattlePlanCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	std::vector<BattleAction> unitActions;
	RecordingBattlePlanCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { unitActions.push_back(action); }
};

class NewHorizonsBattlePlanAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	std::shared_ptr<RecordingBattlePlanCallback> callback;
	std::shared_ptr<BattlePlanAIEnvironment> environment;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void selectBattlePlan()
	{
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		const auto rankLookup = [this](const std::string & queriedSkill)
		{
			return attackerSideHero->getPerkSkillRank(queriedSkill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed, attackerSideHero->getPerkOfferWeightContext());
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == commandSkill
					&& offers[choice].selection.perkId == battlePlanPerk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(commandSkill, battlePlanPerk));
					return;
				}
			}
		}
		FAIL() << "No legal Basic Command Battle Plan offer";
	}

	void activateOwn()
	{
		BattleSetActiveStack packet; packet.battleID = BattleID(0);
		packet.stack = active->unitId(); packet.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(packet);
	}
	void prepareOrdinaryOpening()
	{
		startGame();
		ASSERT_NO_FATAL_FAILURE(selectBattlePlan());
		startBattle();
		ASSERT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase, PreCombatOrderState::Phase::NOT_GRANTED);
		BattleUnitsChanged remove; remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
		auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
		SetStackEffect quiet; quiet.battleID = BattleID(0);
		for(const auto * unit : {active, enemy}) quiet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{
			Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID())});
		gameHandler->sendAndApply(quiet);
		beginCombat(); activateOwn();
		ASSERT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
		ASSERT_EQ(battle()->battleActiveUnit(), active);
		callback = std::make_shared<RecordingBattlePlanCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<BattlePlanAIEnvironment>(gameState());
	}

};
}

TEST_F(NewHorizonsBattlePlanAITest, BooklessCandidatesUseTheOrdinaryActionAndBattlePlanCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepareOrdinaryOpening());
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	EXPECT_EQ(resources.maximum, newHorizonsCommand::maximum(attackerSideHero->level, 1, true, 0));
	const auto started = server.startedActions.size();
	const auto before = gameState()->saveToMemory();
	class PositiveValuation final : public heroOrderCandidates::Valuation
	{
	public:
		float score(const heroOrderCandidates::ValuationRequest &) const override { return 1.0f; }
	} valuation;
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	EXPECT_TRUE(evaluator.canCastSpell());
	std::vector<PossibleSpellcast> candidates;
	heroOrderCandidates::appendOrders(candidates, *callback, BattleID(0), BattleSide::ATTACKER, false, valuation);
	ASSERT_FALSE(candidates.empty());
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(server.startedActions.size(), started);
	const auto chosen = std::find_if(candidates.begin(), candidates.end(), [](const auto & value) { return value.command == HeroCommand::BRACE; });
	ASSERT_NE(chosen, candidates.end());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, chosen->command)));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, resources.remaining - 1);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions, 0u);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleActiveUnit(), active);
}

TEST_F(NewHorizonsBattlePlanAITest, ExhaustedBattlePlanSkipsOrderScoringAndRealAiStillActsWithItsCreature)
{
	ASSERT_NO_FATAL_FAILURE(prepareOrdinaryOpening());
	const int capacity = battle()->getCommandResources(BattleSide::ATTACKER).maximum;
	for(int used = 0; used < capacity; ++used)
	{
		activateOwn();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::BRACE)));
		ASSERT_NO_FATAL_FAILURE(endRound());
	}
	activateOwn();
	ASSERT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, 0);
	ASSERT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions, 1u);
	class CountingValuation final : public heroOrderCandidates::Valuation
	{
	public:
		mutable unsigned calls = 0;
		float score(const heroOrderCandidates::ValuationRequest &) const override { ++calls; return 1.0f; }
	} valuation;
	const auto serialized = [](const auto & value) { CMemorySerializer bytes; bytes.oser & value; return bytes.extractBuffer(); };
	const auto before = gameState()->saveToMemory();
	const auto randomBefore = serialized(*gameHandler->randomizer);
	const auto fixtureBefore = serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto ambientBefore = serialized(CRandomGenerator::getDefault());
	std::vector<PossibleSpellcast> candidates;
	for(const bool mandatory : {false, true})
		heroOrderCandidates::appendOrders(candidates, *callback, BattleID(0), BattleSide::ATTACKER, mandatory, valuation);
	EXPECT_TRUE(candidates.empty()); EXPECT_EQ(valuation.calls, 0u);
	for(const bool enabled : {false, true})
	{
		heroDecisionPressure::Experiment experiment;
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
		EXPECT_FALSE(evaluator.canCastSpell());
		EXPECT_FALSE(evaluator.attemptCastingSpell(active, false, nullptr, nullptr, nullptr, nullptr,
			enabled ? &experiment : nullptr));
		EXPECT_EQ(experiment.baselineAttempts, 0u); EXPECT_FALSE(experiment.baselineReady);
		EXPECT_TRUE(callback->submitted.empty());
		EXPECT_EQ(gameState()->saveToMemory(), before);
		EXPECT_EQ(serialized(*gameHandler->randomizer), randomBefore);
		EXPECT_EQ(serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureBefore);
		EXPECT_EQ(serialized(CRandomGenerator::getDefault()), ambientBefore);
	}
	CBattleAI ai;
	AutocombatPreferences preferences; preferences.enableSpellsUsage = false;
	ai.initBattleInterface(environment, callback, preferences);
	ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0), attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
	ai.activeStack(BattleID(0), active);
	EXPECT_TRUE(callback->submitted.empty());
	ASSERT_EQ(callback->unitActions.size(), 1u);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(serialized(*gameHandler->randomizer), randomBefore);
	EXPECT_EQ(serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureBefore);
	EXPECT_EQ(serialized(CRandomGenerator::getDefault()), ambientBefore);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->unitActions.front()));
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
}
