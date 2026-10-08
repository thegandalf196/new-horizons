/*
 * NewHorizonsGenieSupportAITest.cpp, part of VCMI engine
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
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleUnitTurnReason.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/spells/CSpellHandler.h"

namespace
{
class GenieEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GenieEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class GenieCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> actions;
	GenieCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { actions.push_back(action); }
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
};
}

class NewHorizonsGenieSupportAITest : public HeroCommandFixture
{
protected:
	CStack * genie = nullptr;
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<GenieEnvironment> environment;
	std::shared_ptr<GenieCallback> callback;

	void prepare()
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		genie = addStack(BattleSide::ATTACKER, creatureByName("core:masterGenie"), BattleHex(2, 2), 1);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(9, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:boneDragon"), BattleHex(11, 5), 100);
		beginCombat();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = genie->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
		ASSERT_EQ(battle()->battleActiveUnit(), genie);
		callback = std::make_shared<GenieCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<GenieEnvironment>(gameState());
	}

	BattleEvaluator evaluator()
	{
		return BattleEvaluator(environment, callback, genie, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
	}

	void runAI()
	{
		CBattleAI ai;
		ai.initBattleInterface(environment, callback);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), genie);
	}

	void exhaustPool(CStack * recipient)
	{
		for(const auto spell : battle()->getAvailableBeneficialSpells(genie, recipient))
		{
			auto marker = std::make_shared<Bonus>();
			marker->type = BonusType::NONE;
			marker->duration = BonusDuration::ONE_BATTLE;
			marker->source = BonusSource::SPELL_EFFECT;
			marker->sid = BonusSourceID(spell);
			recipient->addNewBonus(marker);
		}
	}
};

TEST_F(NewHorizonsGenieSupportAITest, ActualAIRecipientRequestUsesAuthoritativeRandomBuffAndSpendsOneActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pool = battle()->getAvailableBeneficialSpells(genie, ally);
	ASSERT_FALSE(pool.empty());
	const auto casts = genie->casts.available();
	const auto mana = attackerSideHero->getManaAvailable();
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	const auto & action = callback->actions.front();
	ASSERT_EQ(action.actionType, EActionType::MONSTER_SPELL);
	ASSERT_EQ(action.stackNumber, genie->unitId());
	ASSERT_EQ(action.target.size(), 1u);
	ASSERT_EQ(action.target.front().unitValue, static_cast<int32_t>(ally->unitId()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(genie->casts.available(), casts - 1);
	EXPECT_NE(battle()->battleActiveUnit(), genie);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(std::any_of(pool.begin(), pool.end(), [&](SpellID spell)
	{
		return ally->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}));
}

TEST_F(NewHorizonsGenieSupportAITest, UnavailableCastsKeepOrdinaryCreaturePolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	genie->casts.use(genie->casts.available());
	auto forecast = evaluator();
	EXPECT_FALSE(forecast.findBestCreatureSpell(genie));
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_NE(callback->actions.front().actionType, EActionType::MONSTER_SPELL);
}

TEST_F(NewHorizonsGenieSupportAITest, ExhaustedRecipientPoolsDoNotSubmitAnotherBuff)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	exhaustPool(genie);
	exhaustPool(ally);
	ASSERT_TRUE(battle()->getAvailableBeneficialSpells(genie, genie).empty());
	ASSERT_TRUE(battle()->getAvailableBeneficialSpells(genie, ally).empty());
	auto forecast = evaluator();
	EXPECT_FALSE(forecast.findBestCreatureSpell(genie));
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_NE(callback->actions.front().actionType, EActionType::MONSTER_SPELL);
}

TEST_F(NewHorizonsGenieSupportAITest, ForecastIsCompleteUniformMeanIncludingZeroOutcomes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto forecast = evaluator();
	const auto pool = battle()->getAvailableBeneficialSpells(genie, ally);
	ASSERT_GT(pool.size(), 1u);
	double sum = 0;
	bool hasZero = false;
	bool hasPositive = false;
	for(const auto spell : pool)
	{
		const float value = forecast.beneficialCreatureSpellOutcomeValue(genie, ally, spell.toSpell());
		sum += value;
		hasZero |= value == 0;
		hasPositive |= value > 0;
	}
	EXPECT_TRUE(hasZero);
	EXPECT_TRUE(hasPositive);
	EXPECT_NEAR(forecast.expectedBeneficialCreatureSpellValue(genie, ally), sum / pool.size(), 0.001);
}

TEST_F(NewHorizonsGenieSupportAITest, RepeatedForecastDoesNotMutateUnitsHeroAllowanceOrEitherLiveRNG)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto genieBefore = genie->save();
	const auto allyBefore = ally->save();
	const auto enemyBefore = enemy->save();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = genie->casts.available();
	gameHandler->randomizer->setSeed(seed);
	const auto expectedServerRandom = gameHandler->getRandomGenerator().nextInt();
	gameHandler->randomizer->setSeed(seed);
	CRandomGenerator::getDefault().setSeed(seed);
	const auto expectedAIRandom = CRandomGenerator::getDefault().nextInt();
	CRandomGenerator::getDefault().setSeed(seed);
	auto forecast = evaluator();
	const auto first = forecast.expectedBeneficialCreatureSpellValue(genie, ally);
	EXPECT_FLOAT_EQ(first, forecast.expectedBeneficialCreatureSpellValue(genie, ally));
	EXPECT_TRUE(forecast.findBestCreatureSpell(genie));
	EXPECT_EQ(genie->save(), genieBefore);
	EXPECT_EQ(ally->save(), allyBefore);
	EXPECT_EQ(enemy->save(), enemyBefore);
	EXPECT_EQ(genie->casts.available(), casts);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleActiveUnit(), genie);
	EXPECT_TRUE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expectedServerRandom);
	EXPECT_EQ(CRandomGenerator::getDefault().nextInt(), expectedAIRandom);
}
