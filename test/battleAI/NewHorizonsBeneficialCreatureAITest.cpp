/*
 * NewHorizonsBeneficialCreatureAITest.cpp, part of VCMI engine
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
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class BeneficialCreatureEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BeneficialCreatureEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class BeneficialCreatureCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> actions;
	BeneficialCreatureCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { actions.push_back(action); }
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
};
}

class NewHorizonsBeneficialCreatureAITest : public HeroCommandFixture
{
protected:
	CStack * ogre = nullptr;
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<BeneficialCreatureEnvironment> environment;
	std::shared_ptr<BeneficialCreatureCallback> callback;

	void prepare()
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		ogre = addStack(BattleSide::ATTACKER, creatureByName("core:ogreMage"), BattleHex(2, 2), 1);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(9, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:boneDragon"), BattleHex(11, 5), 100);
		beginCombat();
		activateOgre();
		ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
		callback = std::make_shared<BeneficialCreatureCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<BeneficialCreatureEnvironment>(gameState());
	}

	void activateOgre()
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = ogre->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_EQ(battle()->battleActiveUnit(), ogre);
	}

	BattleEvaluator evaluator()
	{
		return BattleEvaluator(environment, callback, ogre, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
	}

	void runAI()
	{
		CBattleAI ai;
		ai.initBattleInterface(environment, callback);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), ogre);
	}

	void applyBloodlust(CStack * recipient)
	{
		spells::BattleCast cast(battle(), ogre, spells::Mode::CREATURE_ACTIVE, SpellID(SpellID::BLOODLUST).toSpell());
		cast.setSpellLevel(MasteryLevel::ADVANCED);
		cast.cast(gameHandler->spellEnv.get(), {spells::Destination(recipient)});
	}
};

TEST_F(NewHorizonsBeneficialCreatureAITest, ActualOgreAIRequestAppliesAdvancedBloodlustAndSpendsOnlyCreatureCastActivation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(ogre->getSpellSchoolLevel(SpellID(SpellID::BLOODLUST).toSpell()), MasteryLevel::ADVANCED);
	const auto casts = ogre->casts.available();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto round = battle()->getRound();
	const auto allowances = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round);
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	const auto & action = callback->actions.front();
	ASSERT_EQ(action.actionType, EActionType::MONSTER_SPELL);
	ASSERT_EQ(action.spell, SpellID::BLOODLUST);
	ASSERT_EQ(action.target.size(), 1u);
	ASSERT_EQ(action.target.front().unitValue, static_cast<int32_t>(ally->unitId()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const auto effects = ally->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLOODLUST))));
	ASSERT_EQ(effects->size(), 1u);
	EXPECT_EQ(effects->front()->type, BonusType::PRIMARY_SKILL);
	EXPECT_EQ(effects->front()->val, 6);
	EXPECT_EQ(effects->front()->turnsRemain, 3);
	EXPECT_EQ(ogre->casts.available(), casts - 1);
	EXPECT_NE(battle()->battleActiveUnit(), ogre);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round), allowances);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsBeneficialCreatureAITest, NoCastsRetainsOrdinaryCreatureAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ogre->casts.use(ogre->casts.available());
	auto forecast = evaluator();
	EXPECT_FALSE(forecast.findBestCreatureSpell(ogre));
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_NE(callback->actions.front().actionType, EActionType::MONSTER_SPELL);
}

TEST_F(NewHorizonsBeneficialCreatureAITest, ExistingBloodlustHasNoAdditionalPressureAndIsNotSelected)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	applyBloodlust(ally);
	applyBloodlust(ogre);
	activateOgre();
	ASSERT_TRUE(ogre->canCast());
	auto forecast = evaluator();
	EXPECT_FLOAT_EQ(forecast.beneficialCreatureSpellOutcomeValue(ogre, ally,
		SpellID(SpellID::BLOODLUST).toSpell()), 0);
	EXPECT_FALSE(forecast.findBestCreatureSpell(ogre));
}

TEST_F(NewHorizonsBeneficialCreatureAITest, ZeroHPBuffPreviewIsPositiveAndDoesNotSpendStateOrRNG)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto beforeOgre = ogre->save();
	const auto beforeAlly = ally->save();
	const auto beforeEnemy = enemy->save();
	const auto mana = attackerSideHero->getManaAvailable();
	gameHandler->randomizer->setSeed(seed);
	const int serverRandom = gameHandler->getRandomGenerator().nextInt();
	gameHandler->randomizer->setSeed(seed);
	CRandomGenerator::getDefault().setSeed(seed);
	const int aiRandom = CRandomGenerator::getDefault().nextInt();
	CRandomGenerator::getDefault().setSeed(seed);
	auto forecast = evaluator();
	PossibleSpellcast oldScore;
	oldScore.spell = SpellID(SpellID::BLOODLUST).toSpell();
	oldScore.dest = {spells::Destination(ally)};
	forecast.evaluateCreatureSpellcast(ogre, oldScore);
	ASSERT_FLOAT_EQ(oldScore.value, 0);
	const auto first = forecast.findBestCreatureSpell(ogre);
	const auto second = forecast.findBestCreatureSpell(ogre);
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	EXPECT_GT(first->value, 0);
	EXPECT_FLOAT_EQ(first->value, second->value);
	EXPECT_EQ(ogre->save(), beforeOgre);
	EXPECT_EQ(ally->save(), beforeAlly);
	EXPECT_EQ(enemy->save(), beforeEnemy);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), serverRandom);
	EXPECT_EQ(CRandomGenerator::getDefault().nextInt(), aiRandom);
}

TEST_F(NewHorizonsBeneficialCreatureAITest, SchoolSpecificMasteryDoesNotReplaceCreatureAbilityMastery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto forecast = evaluator();
	const auto * bloodlust = SpellID(SpellID::BLOODLUST).toSpell();
	const auto advancedValue = forecast.beneficialCreatureSpellOutcomeValue(ogre, ally, bloodlust);
	auto school = std::make_shared<Bonus>();
	school->type = BonusType::MAGIC_SCHOOL_SKILL;
	school->subtype = BonusSubtypeID(SpellSchool::FIRE);
	school->val = MasteryLevel::EXPERT;
	ogre->addNewBonus(school);
	EXPECT_EQ(ogre->getSpellSchoolLevel(bloodlust), MasteryLevel::ADVANCED);
	EXPECT_FLOAT_EQ(forecast.beneficialCreatureSpellOutcomeValue(ogre, ally, bloodlust), advancedValue);
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	ASSERT_EQ(callback->actions.front().actionType, EActionType::MONSTER_SPELL);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->actions.front()));
	const auto effects = ally->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::BLOODLUST))));
	ASSERT_EQ(effects->size(), 1u);
	EXPECT_EQ(effects->front()->val, 6);
	EXPECT_EQ(effects->front()->turnsRemain, 3);
}
