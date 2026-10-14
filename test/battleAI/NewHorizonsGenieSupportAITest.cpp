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
#include "../../lib/bonuses/BonusParameters.h"
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

	void prepare(bool upgraded = false, const char * enemyCreature = "core:boneDragon")
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		genie = addStack(BattleSide::ATTACKER, creatureByName(upgraded ? "core:masterGenie" : "core:genie"), BattleHex(2, 2), 1);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(9, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName(enemyCreature), BattleHex(11, 5), 100);
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

	bool wish(CStack * recipient)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeCreatureSpellcast(genie, {spells::Destination(recipient)}, SpellID::NONE));
	}

	void reactivateGenie()
	{
		advanceRound();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = genie->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}
};

TEST_F(NewHorizonsGenieSupportAITest, RuntimeHostilePolicyRejectsMalformedExternallyConstructedMetadata)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	JsonNode vector;
	vector.Vector().emplace_back(1);
	JsonNode object;
	object["policy"].Integer() = 1;
	JsonNode integralFloat;
	integralFloat.Float() = 1.0;
	const std::vector<JsonNode> policies{
		JsonNode(), JsonNode(0), JsonNode(true), JsonNode("1"), JsonNode(2), JsonNode(-1),
		JsonNode(int64_t{1} << 40), JsonNode(1.5), integralFloat, vector, object,
		JsonNode(std::numeric_limits<double>::infinity()),
		JsonNode(std::numeric_limits<double>::quiet_NaN())
	};
	for(const auto & policy : policies)
	{
		auto ability = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::RANDOM_SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID());
		ability->parameters = std::make_shared<BonusParameters>(policy);
		genie->addNewBonus(ability);
		ASSERT_EQ(genie->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER)).get(), ability.get());
		EXPECT_TRUE(battle()->getAvailableRandomCreatureSpells(genie, enemy).empty());
		genie->removeBonus(ability);
	}
}

TEST_F(NewHorizonsGenieSupportAITest, MixedCreatureProficienciesUseChosenSpellBonusAndAnySchoolRaise)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	const auto * curse = SpellID(SpellID::CURSE).toSpell();
	genie->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(haste->getId())));
	genie->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELLCASTER, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(curse->getId())));
	genie->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::SPELLCASTER, BonusSource::OTHER, 2, BonusSourceID(), BonusSubtypeID(curse->getId())));
	// The server uses the first specific ability, not their summed proficiency.
	EXPECT_EQ(BattleEvaluator::creatureAbilitySpellLevel(genie, haste, 2), 3);
	EXPECT_EQ(BattleEvaluator::creatureAbilitySpellLevel(genie, curse, 2), 2);
	genie->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	EXPECT_EQ(BattleEvaluator::creatureAbilitySpellLevel(genie, haste, 2), 3);
	EXPECT_EQ(BattleEvaluator::creatureAbilitySpellLevel(genie, curse, 2), 3);
	const auto pool = battle()->getAvailableRandomCreatureSpells(genie, ally);
	ASSERT_FALSE(pool.empty());
	auto forecast = evaluator();
	double sum = 0;
	for(const auto spell : pool)
		sum += forecast.beneficialCreatureSpellOutcomeValue(genie, ally, spell.toSpell());
	EXPECT_NEAR(forecast.expectedBeneficialCreatureSpellValue(genie, ally), sum / pool.size(), 0.001);
}

TEST_F(NewHorizonsGenieSupportAITest, RegularGenieRetainsThreeAlliedWishesAndRejectsHostileTargetsWithoutCost)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(genie->casts.available(), 3);
	EXPECT_FALSE(battle()->getAvailableRandomCreatureSpells(genie, ally).empty());
	EXPECT_TRUE(battle()->getAvailableRandomCreatureSpells(genie, enemy).empty());
	gameHandler->randomizer->setSeed(seed);
	const auto expectedRandom = gameHandler->getRandomGenerator().nextInt();
	gameHandler->randomizer->setSeed(seed);
	EXPECT_FALSE(wish(enemy));
	EXPECT_EQ(genie->casts.available(), 3);
	EXPECT_EQ(battle()->battleActiveUnit(), genie);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expectedRandom);
}

TEST_F(NewHorizonsGenieSupportAITest, UpgradedGenieUsesOneSharedChargePoolForAlliedAndHostileWishes)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_EQ(genie->casts.available(), 3);
	const auto positive = battle()->getAvailableRandomCreatureSpells(genie, ally);
	const auto negative = battle()->getAvailableRandomCreatureSpells(genie, enemy);
	ASSERT_FALSE(positive.empty());
	ASSERT_FALSE(negative.empty());
	for(const auto spell : positive)
		EXPECT_TRUE(spell.toSpell()->isPositive());
	for(const auto spell : negative)
	{
		EXPECT_TRUE(spell.toSpell()->isNegative());
		EXPECT_FALSE(spell.toSpell()->isDamage());
	}
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(wish(enemy));
	EXPECT_EQ(genie->casts.available(), 2);
	EXPECT_TRUE(std::any_of(negative.begin(), negative.end(), [&](SpellID spell)
	{
		return enemy->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}));
	reactivateGenie();
	ASSERT_TRUE(wish(ally));
	EXPECT_EQ(genie->casts.available(), 1);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(std::any_of(positive.begin(), positive.end(), [&](SpellID spell)
	{
		return ally->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}));
	reactivateGenie();
	ASSERT_TRUE(wish(enemy));
	EXPECT_EQ(genie->casts.available(), 0);
	reactivateGenie();
	EXPECT_FALSE(wish(ally));
	EXPECT_EQ(genie->casts.available(), 0);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsGenieSupportAITest, ZeroBenefitBoneDragonHostileForecastKeepsOrdinaryCreaturePolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	exhaustPool(genie);
	exhaustPool(ally);
	const auto pool = battle()->getAvailableRandomCreatureSpells(genie, enemy);
	ASSERT_FALSE(pool.empty());
	EXPECT_EQ(pool, (std::vector<SpellID>{SpellID::MISFORTUNE, SpellID::SLOW}));
	const auto before = enemy->save();
	gameHandler->randomizer->setSeed(seed);
	const auto expectedRandom = gameHandler->getRandomGenerator().nextInt();
	gameHandler->randomizer->setSeed(seed);
	CRandomGenerator::getDefault().setSeed(seed);
	const auto expectedAIRandom = CRandomGenerator::getDefault().nextInt();
	CRandomGenerator::getDefault().setSeed(seed);
	auto forecast = evaluator();
	double sum = 0;
	int positiveOutcomes = 0;
	RecordProperty("hostile_fixture_enemy", enemy->unitType()->getJsonKey());
	RecordProperty("hostile_fixture_enemy_count", enemy->getCount());
	RecordProperty("hostile_fixture_ally", ally->unitType()->getJsonKey());
	RecordProperty("hostile_fixture_ally_count", ally->getCount());
	RecordProperty("hostile_legal_pool_size", static_cast<int>(pool.size()));
	for(const auto spell : pool)
	{
		const auto outcome = forecast.beneficialCreatureSpellOutcomeValue(genie, enemy, spell.toSpell());
		sum += outcome;
		positiveOutcomes += outcome > 0 ? 1 : 0;
		RecordProperty("hostile_outcome_" + std::to_string(spell.num),
			spell.toSpell()->getJsonKey() + "=" + std::to_string(outcome));
	}
	const auto expectedMean = forecast.expectedBeneficialCreatureSpellValue(genie, enemy);
	RecordProperty("hostile_expected_mean", std::to_string(expectedMean));
	RecordProperty("hostile_uniform_mean", std::to_string(sum / pool.size()));
	RecordProperty("hostile_positive_outcomes", positiveOutcomes);
	EXPECT_NEAR(expectedMean, sum / pool.size(), 0.001);
	EXPECT_FLOAT_EQ(expectedMean, 0);
	EXPECT_EQ(positiveOutcomes, 0);
	const auto candidate = forecast.findBestCreatureSpell(genie);
	RecordProperty("hostile_candidate_available", candidate.has_value() ? 1 : 0);
	EXPECT_FALSE(candidate);
	EXPECT_EQ(enemy->save(), before);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expectedRandom);
	EXPECT_EQ(CRandomGenerator::getDefault().nextInt(), expectedAIRandom);
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	EXPECT_NE(callback->actions.front().actionType, EActionType::MONSTER_SPELL);
	EXPECT_EQ(genie->casts.available(), 3);
}

TEST_F(NewHorizonsGenieSupportAITest, PositiveHydraHostileForecastUsesUniformPoolAndActualAIRequestsEnemy)
{
	// A living variable-damage Hydra can reach the large allied army, while
	// the distant one-creature caster lies outside its ordinary movement range.
	// Curse therefore has a marginal physical benefit rather than an overkill
	// or undead-immunity fixture that demands an otherwise worthless wish.
	ASSERT_NO_FATAL_FAILURE(prepare(true, "core:hydra"));
	exhaustPool(genie);
	exhaustPool(ally);
	const auto pool = battle()->getAvailableRandomCreatureSpells(genie, enemy);
	ASSERT_FALSE(pool.empty());
	ASSERT_TRUE(vstd::contains(pool, SpellID(SpellID::CURSE)));
	const auto genieBefore = genie->save();
	const auto allyBefore = ally->save();
	const auto enemyBefore = enemy->save();
	gameHandler->randomizer->setSeed(seed);
	const auto expectedRandom = gameHandler->getRandomGenerator().nextInt();
	gameHandler->randomizer->setSeed(seed);
	CRandomGenerator::getDefault().setSeed(seed);
	const auto expectedAIRandom = CRandomGenerator::getDefault().nextInt();
	CRandomGenerator::getDefault().setSeed(seed);
	auto forecast = evaluator();
	double sum = 0;
	int positiveOutcomes = 0;
	RecordProperty("hostile_fixture_enemy", enemy->unitType()->getJsonKey());
	RecordProperty("hostile_fixture_enemy_count", enemy->getCount());
	RecordProperty("hostile_fixture_ally", ally->unitType()->getJsonKey());
	RecordProperty("hostile_fixture_ally_count", ally->getCount());
	RecordProperty("hostile_legal_pool_size", static_cast<int>(pool.size()));
	for(const auto spell : pool)
	{
		const auto outcome = forecast.beneficialCreatureSpellOutcomeValue(genie, enemy, spell.toSpell());
		sum += outcome;
		positiveOutcomes += outcome > 0 ? 1 : 0;
		RecordProperty("hostile_outcome_" + std::to_string(spell.num),
			spell.toSpell()->getJsonKey() + "=" + std::to_string(outcome));
	}
	const auto curseWitness = forecast.beneficialCreatureSpellOutcomeValue(genie, enemy,
		SpellID(SpellID::CURSE).toSpell());
	RecordProperty("hostile_curse_positive_witness", std::to_string(curseWitness));
	EXPECT_GT(curseWitness, 0);
	const auto expectedMean = forecast.expectedBeneficialCreatureSpellValue(genie, enemy);
	RecordProperty("hostile_expected_mean", std::to_string(expectedMean));
	RecordProperty("hostile_uniform_mean", std::to_string(sum / pool.size()));
	RecordProperty("hostile_positive_outcomes", positiveOutcomes);
	EXPECT_NEAR(expectedMean, sum / pool.size(), 0.001);
	EXPECT_GT(expectedMean, 0);
	const auto candidate = forecast.findBestCreatureSpell(genie);
	RecordProperty("hostile_candidate_available", candidate.has_value() ? 1 : 0);
	EXPECT_EQ(genie->save(), genieBefore);
	EXPECT_EQ(ally->save(), allyBefore);
	EXPECT_EQ(enemy->save(), enemyBefore);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expectedRandom);
	EXPECT_EQ(CRandomGenerator::getDefault().nextInt(), expectedAIRandom);
	ASSERT_TRUE(candidate);
	ASSERT_EQ(candidate->dest.size(), 1u);
	EXPECT_EQ(candidate->dest.front().unitValue, enemy);
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	const auto & action = callback->actions.front();
	ASSERT_EQ(action.actionType, EActionType::MONSTER_SPELL);
	ASSERT_EQ(action.target.size(), 1u);
	EXPECT_EQ(action.target.front().unitValue, static_cast<int32_t>(enemy->unitId()));
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(genie->casts.available(), 2);
}

TEST_F(NewHorizonsGenieSupportAITest, FullyImmuneHostileTargetHasNoPoolAndDoesNotConsumeCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto pool = battle()->getAvailableRandomCreatureSpells(genie, enemy);
	ASSERT_FALSE(pool.empty());
	for(const auto spell : pool)
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell)));
	EXPECT_TRUE(battle()->getAvailableRandomCreatureSpells(genie, enemy).empty());
	EXPECT_FALSE(wish(enemy));
	EXPECT_EQ(genie->casts.available(), 3);
	EXPECT_EQ(battle()->battleActiveUnit(), genie);
}

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
