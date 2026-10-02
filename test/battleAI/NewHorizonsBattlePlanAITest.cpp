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
#include "../../lib/GameConstants.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/HeroCommand.h"
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
	RecordingBattlePlanCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
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
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void selectBattlePlan()
	{
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
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
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

	void preparePendingOpening()
	{
		startGame();
		selectBattlePlan();
		startBattle();
		ASSERT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
			PreCombatOrderState::Phase::AVAILABLE);
		beginCombat();
		const auto & pending = battle()->getPreCombatOrderState(BattleSide::ATTACKER);
		ASSERT_EQ(pending.phase, PreCombatOrderState::Phase::ORDER_REQUIRED);
		ASSERT_TRUE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
		active = battle()->getStack(static_cast<int>(pending.anchorStackId), false);
		ASSERT_NE(active, nullptr);
		ASSERT_EQ(battle()->battleActiveUnit(), active);

		callback = std::make_shared<RecordingBattlePlanCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<BattlePlanAIEnvironment>(gameState());
	}
};
}

TEST_F(NewHorizonsBattlePlanAITest, BooklessEvaluatorSubmitsAndServerAcceptsTheMandatoryOpeningOrder)
{
	preparePendingOpening();
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	ASSERT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(1).heroActions, 1u);
	ASSERT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(1).orderActions, 1u);
	const auto startedBeforeEvaluation = server.startedActions.size();

	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	ASSERT_TRUE(evaluator.canCastSpell())
		<< "A pending Battle Plan is a mandatory Order choice, not an optional spell decision";
	ASSERT_TRUE(evaluator.attemptCastingSpell(active, true))
		<< "The opening choice must submit an Order without a spellbook or ordinary creature forecast";

	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & selected = callback->submitted.front();
	ASSERT_EQ(selected.actionType, EActionType::HERO_COMMAND);
	ASSERT_EQ(selected.side, BattleSide::ATTACKER);
	EXPECT_TRUE(heroCommands::isActive(selected.command));
	EXPECT_EQ(server.startedActions.size(), startedBeforeEvaluation)
		<< "AI evaluation must not mutate the authoritative battle before submitting its request";
	EXPECT_TRUE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), selected));
	EXPECT_EQ(server.startedActions.size(), startedBeforeEvaluation + 1);
	EXPECT_EQ(server.startedActions.back().ba.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(battle()->getPreCombatOrderState(BattleSide::ATTACKER).phase,
		PreCombatOrderState::Phase::COMPLETED);
	EXPECT_FALSE(battle()->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(1).heroActions, 1u);
}
