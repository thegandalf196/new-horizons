/*
 * NewHorizonsDoubleCommandAITest.cpp, part of VCMI engine
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
#include "../../lib/CCreatureHandler.h"
#include "../../lib/StartInfo.h"
#include "../../lib/CRandomGenerator.h"
#include "../../AI/BattleAI/HeroOrderCandidates.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto doubleCommandPerk = "new-horizons:command.doubleCommand";
constexpr auto basicCommandPerk = "new-horizons:command.aggressiveCommander";
constexpr auto advancedCommandPerk = "new-horizons:command.veteranCommander";

class DoubleCommandAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DoubleCommandAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class RecordingDoubleCommandCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> heroActions;
	std::vector<BattleAction> creatureActions;

	RecordingDoubleCommandCallback() : CBattleCallback(PlayerColor(0), nullptr) {}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		heroActions.push_back(action);
	}

	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override
	{
		creatureActions.push_back(action);
	}
};
}

class NewHorizonsDoubleCommandAITest : public HeroCommandFixture
{
protected:
	CStack * active = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<DoubleCommandAIEnvironment> environment;
	std::shared_ptr<RecordingDoubleCommandCallback> callback;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void acceptPerk(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == commandSkill && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(commandSkill, perkId));
					return;
				}
			}
		}
		FAIL() << "No legal perk offer for " << perkId;
	}

	void selectDoubleCommand(CGHeroInstance * hero)
	{
		hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, basicCommandPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, advancedCommandPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, doubleCommandPerk);
		EXPECT_TRUE(hero->hasActivePerk(commandSkill, basicCommandPerk));
		EXPECT_TRUE(hero->hasActivePerk(commandSkill, advancedCommandPerk));
	}

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
	}


	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	void prepareSpentOrdinaryAction()
	{
		startGame();
		selectDoubleCommand(attackerSideHero);
		startBattle();
		clearStartingUnits();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();

		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleActiveUnit(), active);

		ASSERT_TRUE(issue(HeroCommand::CHARGE));
		ASSERT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
		ASSERT_EQ(battle()->getDoubleCommandState(BattleSide::ATTACKER), DoubleCommandState{});

		callback = std::make_shared<RecordingDoubleCommandCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<DoubleCommandAIEnvironment>(gameState());
	}
};

TEST_F(NewHorizonsDoubleCommandAITest, MasterCommanderCannotSubmitAnOrderAfterTheOrdinaryActionWasSpent)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpentOrdinaryAction());
	ASSERT_TRUE(attackerSideHero->hasActivePerk(commandSkill, doubleCommandPerk));
	const auto counts = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound());
	ASSERT_EQ(counts.heroActions, 0u);
	ASSERT_EQ(counts.orderActions, 0u);
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	ASSERT_GT(resources.remaining, 0);
	const auto before = gameState()->saveToMemory();
	const auto started = server.startedActions.size();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	EXPECT_FALSE(evaluator.canCastSpell());
	EXPECT_FALSE(evaluator.attemptCastingSpell(active, true));
	EXPECT_TRUE(callback->heroActions.empty());
	EXPECT_TRUE(callback->creatureActions.empty());
	EXPECT_FALSE(issue(HeroCommand::BRACE));
	EXPECT_EQ(server.startedActions.size(), started);
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	const auto orders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(orders.size(), 1u);
	EXPECT_EQ(orders.front().command, HeroCommand::CHARGE);
}

TEST_F(NewHorizonsDoubleCommandAITest, MandatoryFlagCannotBypassOrdinaryAllowanceBeforeHeuristicScoring)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpentOrdinaryAction());
	class CountingValuation final : public heroOrderCandidates::Valuation
	{
	public:
		mutable unsigned calls = 0;
		float score(const heroOrderCandidates::ValuationRequest &) const override { ++calls; return -7.0f; }
	} valuation;
	const auto ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto orders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	const auto before = gameState()->saveToMemory();
	const auto started = server.startedActions.size();
	std::vector<PossibleSpellcast> optional, mandatory;
	heroOrderCandidates::appendOrders(optional, *callback, BattleID(0), BattleSide::ATTACKER, false, valuation);
	heroOrderCandidates::appendOrders(mandatory, *callback, BattleID(0), BattleSide::ATTACKER, true, valuation);
	EXPECT_TRUE(optional.empty()); EXPECT_TRUE(mandatory.empty());
	EXPECT_EQ(valuation.calls, 0u);
	const auto wire = [](const auto & value) { CMemorySerializer bytes; bytes.oser & value; return bytes.extractBuffer(); };
	EXPECT_EQ(wire(battle()->getHeroActionAllowances(BattleSide::ATTACKER)), wire(ledger));
	EXPECT_EQ(wire(battle()->getDoubleCommandState(BattleSide::ATTACKER)), wire(DoubleCommandState{}));
	EXPECT_EQ(wire(battle()->battleGetHeroOrderStates(BattleSide::ATTACKER)), wire(orders));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(server.startedActions.size(), started);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_TRUE(callback->heroActions.empty()); EXPECT_TRUE(callback->creatureActions.empty());
}

TEST_F(NewHorizonsDoubleCommandAITest, RealActiveStackContinuesWithCreatureActionWithoutAnotherOrderDecision)
{
	ASSERT_NO_FATAL_FAILURE(prepareSpentOrdinaryAction());
	const auto serialized = [](const auto & value) { CMemorySerializer bytes; bytes.oser & value; return bytes.extractBuffer(); };
	const auto before = gameState()->saveToMemory();
	const auto randomBefore = serialized(*gameHandler->randomizer);
	const auto fixtureBefore = serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto ambientBefore = serialized(CRandomGenerator::getDefault());
	for(const bool enabled : {false, true})
	{
		heroDecisionPressure::Experiment experiment;
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
		EXPECT_FALSE(evaluator.attemptCastingSpell(active, false, nullptr, nullptr, nullptr, nullptr,
			enabled ? &experiment : nullptr));
		EXPECT_EQ(experiment.baselineAttempts, 0u);
		EXPECT_FALSE(experiment.baselineReady);
		EXPECT_TRUE(callback->heroActions.empty());
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
	EXPECT_TRUE(callback->heroActions.empty());
	ASSERT_EQ(callback->creatureActions.size(), 1u);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(serialized(*gameHandler->randomizer), randomBefore);
	EXPECT_EQ(serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureBefore);
	EXPECT_EQ(serialized(CRandomGenerator::getDefault()), ambientBefore);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->creatureActions.front()));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
}
