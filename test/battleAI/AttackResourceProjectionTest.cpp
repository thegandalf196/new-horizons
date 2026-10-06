/*
 * AttackResourceProjectionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include <atomic>
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/spells/NewHorizonsSorcery.h"

namespace
{
class AttackEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AttackEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class AmmoOveruseLogTarget final : public ILogTarget
{
public:
	explicit AmmoOveruseLogTarget(std::weak_ptr<std::atomic_size_t> warningCount)
		: warningCount(std::move(warningCount))
	{
	}

	void write(const LogRecord & record) override
	{
		const auto counter = warningCount.lock();
		if(counter && record.message.find("Stack ammo overuse") != std::string::npos)
			counter->fetch_add(1);
	}

private:
	std::weak_ptr<std::atomic_size_t> warningCount;
};
}

class AttackResourceProjectionTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<AttackEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;
	std::shared_ptr<HypotheticBattle> model;
	DamageCache cache;

	void prepareModel()
	{
		environment = std::make_shared<AttackEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		model = std::make_shared<HypotheticBattle>(environment.get(), callback);
		cache.buildDamageCache(model, BattleSide::ATTACKER);
	}
};

TEST_F(AttackResourceProjectionTest, FutureProductionLoopRetaliatesOnlyOnFirstStrike)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * opening = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(7, 5), 1000);
	auto * followup = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(8, 4), 1000);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(8, 5), 1000);
	Bonus speed;
	speed.type = BonusType::STACKS_SPEED;
	speed.val = 20;
	opening->addNewBonus(std::make_shared<Bonus>(speed));
	speed.val = 10;
	followup->addNewBonus(std::make_shared<Bonus>(speed));
	Bonus extra;
	extra.type = BonusType::ADDITIONAL_ATTACK;
	extra.val = 1;
	followup->addNewBonus(std::make_shared<Bonus>(extra));
	extra.type = BonusType::ADDITIONAL_RETALIATION;
	extra.val = 2;
	defender->addNewBonus(std::make_shared<Bonus>(extra));
	defender->movedThisRound = true;
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = opening->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_EQ(defender->counterAttacks.available(), 3);
	ASSERT_EQ(followup->getTotalAttacks(false), 2);
	prepareModel();
	ASSERT_TRUE(model->isMeleeAttackPossible(opening, defender));
	ASSERT_TRUE(model->isMeleeAttackPossible(followup, defender));
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(opening, defender, 0, false),
		opening->getPosition(), cache, model);
	PotentialTargets targets(opening, cache, model);
	BattleExchangeEvaluator evaluator(callback, environment, 1.0f, 1);
	evaluator.updateReachabilityMap(model);
	const auto units = evaluator.getExchangeUnits(attack, 0, targets, model);
	ASSERT_EQ(units.units.size(), 2u);
	ASSERT_EQ(units.units.at(0).size(), 2u);
	ASSERT_EQ(units.units.at(0).at(0)->unitId(), opening->unitId());
	ASSERT_EQ(units.units.at(0).at(1)->unitId(), followup->unitId());
	const auto actual = evaluator.evaluateExchange(attack, 0, targets, cache, model);

	const auto referenceScore = [&](bool repeatRetaliation)
	{
		auto reference = std::make_shared<HypotheticBattle>(environment.get(), callback);
		BattleExchangeVariant expected;
		expected.trackAttack(attack, reference, cache);
		for(int index = 0; index < 2; ++index)
			expected.trackAttack(reference->getForUpdate(followup->unitId()), reference->getForUpdate(defender->unitId()),
				false, true, cache, reference, false, repeatRetaliation || index == 0);
		// One simulated round is scaled over the two-round reachability horizon.
		return (expected.getScore().enemyDamageReduce - expected.getScore().ourDamageReduce) / 2.0f;
	};
	const auto expected = referenceScore(false);
	const auto repeated = referenceScore(true);
	ASSERT_GT(expected, repeated);
	EXPECT_FLOAT_EQ(actual, expected);
	EXPECT_EQ(defender->counterAttacks.available(), 3);
}

TEST_F(AttackResourceProjectionTest, MeleeExchangeCandidateScoringDoesNotSpendAmmunition)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);

	auto * opening = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(7, 5), 1000);
	auto * followup = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(8, 4), 1000);
	auto * enemyOgre = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(8, 5), 1000);
	Bonus freeShooting;
	freeShooting.type = BonusType::FREE_SHOOTING;
	freeShooting.val = 1;
	opening->addNewBonus(std::make_shared<Bonus>(freeShooting));
	followup->addNewBonus(std::make_shared<Bonus>(freeShooting));
	Bonus speed;
	speed.type = BonusType::STACKS_SPEED;
	speed.val = 20;
	opening->addNewBonus(std::make_shared<Bonus>(speed));
	speed.val = 10;
	followup->addNewBonus(std::make_shared<Bonus>(speed));

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = opening->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	prepareModel();
	ASSERT_TRUE(model->battleCanShoot(opening));
	ASSERT_TRUE(model->battleCanShoot(followup));
	ASSERT_FALSE(model->battleCanShoot(enemyOgre));

	const auto openingShots = opening->shots.available();
	const auto followupShots = followup->shots.available();
	const auto enemyShots = enemyOgre->shots.available();
	ASSERT_EQ(enemyShots, 0);
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(opening, enemyOgre, 0, true),
		opening->getPosition(), cache, model);
	ASSERT_NE(attack.attackerState, nullptr);
	PotentialTargets targets(opening, cache, model);
	BattleExchangeEvaluator evaluator(callback, environment, 1.0f, 1);
	evaluator.updateReachabilityMap(model);
	const auto units = evaluator.getExchangeUnits(attack, 0, targets, model);
	const auto isScheduled = [&units](const battle::Unit * unit)
	{
		return std::any_of(units.units.begin(), units.units.end(), [unit](const auto & entry)
		{
			return vstd::contains(entry.second, unit);
		});
	};
	ASSERT_TRUE(isScheduled(opening));
	ASSERT_TRUE(isScheduled(followup));
	ASSERT_TRUE(isScheduled(enemyOgre));
	ASSERT_TRUE(vstd::contains(units.melleeAccessible, opening));
	ASSERT_TRUE(vstd::contains(units.melleeAccessible, followup));
	ASSERT_FALSE(vstd::contains(units.shooters, opening));
	ASSERT_FALSE(vstd::contains(units.shooters, followup));

	auto ammoOveruseWarnings = std::make_shared<std::atomic_size_t>(0);
	CLogger::getGlobalLogger()->addTarget(std::make_unique<AmmoOveruseLogTarget>(ammoOveruseWarnings));
	evaluator.evaluateExchange(attack, 0, targets, cache, model);

	EXPECT_EQ(ammoOveruseWarnings->load(), 0u);
	EXPECT_EQ(opening->shots.available(), openingShots);
	EXPECT_EQ(followup->shots.available(), followupShots);
	EXPECT_EQ(enemyOgre->shots.available(), enemyShots);
}

TEST_F(AttackResourceProjectionTest, TwoStrikeSequenceAllowsOnlyOneRetaliationLikeAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(7, 5), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(8, 5), 100);
	Bonus extraAttack;
	extraAttack.type = BonusType::ADDITIONAL_ATTACK;
	extraAttack.val = 1;
	attacker->addNewBonus(std::make_shared<Bonus>(extraAttack));
	Bonus extraRetaliation;
	extraRetaliation.type = BonusType::ADDITIONAL_RETALIATION;
	extraRetaliation.val = 1;
	defender->addNewBonus(std::make_shared<Bonus>(extraRetaliation));
	ASSERT_EQ(attacker->getTotalAttacks(false), 2);
	ASSERT_EQ(defender->counterAttacks.available(), 2);
	prepareModel();
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(attacker, defender, 0, false),
		attacker->getPosition(), cache, model);
	const auto affected = std::find_if(attack.affectedUnits.begin(), attack.affectedUnits.end(),
		[&](const auto & unit){ return unit->unitId() == defender->unitId(); });
	ASSERT_NE(affected, attack.affectedUnits.end());
	ASSERT_TRUE((*affected)->alive());
	EXPECT_EQ((*affected)->counterAttacks.available(), 1);
	BattleExchangeVariant exchange;
	exchange.trackAttack(attack, model, cache);
	EXPECT_EQ(model->getForUpdate(defender->unitId())->counterAttacks.available(), 1);

	auto direct = std::make_shared<HypotheticBattle>(environment.get(), callback);
	BattleExchangeVariant future;
	for(int index = 0; index < 2; ++index)
		future.trackAttack(direct->getForUpdate(attacker->unitId()), direct->getForUpdate(defender->unitId()),
			false, true, cache, direct, false, index == 0);
	EXPECT_EQ(direct->getForUpdate(defender->unitId())->counterAttacks.available(), 1);
	EXPECT_EQ(defender->counterAttacks.available(), 2);

	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = attacker->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition())));
	ASSERT_TRUE(defender->alive());
	EXPECT_EQ(defender->counterAttacks.available(), 1);
}

TEST_F(AttackResourceProjectionTest, TrackingTwoShotPreviewConsumesBothShotsOnlyInModel)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 5), 10);
	const auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	const auto available = shooter->shots.available();
	ASSERT_GE(available, 2);
	prepareModel();
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(shooter, defender, 0, true),
		shooter->getPosition(), cache, model);
	ASSERT_NE(attack.attackerState, nullptr);
	ASSERT_EQ(attack.attackerState->shots.available(), available - 2);
	BattleExchangeVariant exchange;
	exchange.trackAttack(attack, model, cache);
	EXPECT_EQ(model->getForUpdate(shooter->unitId())->shots.available(), available - 2);
	EXPECT_EQ(shooter->shots.available(), available);
}

TEST_F(AttackResourceProjectionTest, FocusMagicMarksApplyBetweenShotsWithoutMutatingTheSourceBattle)
{
	const auto focus = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		std::string(newHorizonsSorcery::FOCUS_MAGIC_SPELL));
	if(!focus)
		GTEST_SKIP() << "Requires New Horizons Focus Magic content";
	const auto trigger = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
		std::string(newHorizonsSorcery::FOCUS_MAGIC_TRIGGER));
	const auto marker = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
		std::string(newHorizonsSorcery::ARCANE_BREACH_TRIGGER));
	ASSERT_TRUE(trigger);
	ASSERT_TRUE(marker);
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	forceMaximumDamage(shooter);
	auto enchantment = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
		BonusSource::SPELL_EFFECT, 2000, BonusSourceID(SpellID(*focus)), BonusSubtypeID(ScriptID(*trigger)));
	enchantment->turnsRemain = 3;
	JsonNode parameters;
	parameters["beneficiarySide"].Integer() = static_cast<int>(BattleSide::ATTACKER);
	enchantment->parameters = std::make_shared<BonusParameters>(parameters);
	shooter->addNewBonus(enchantment);
	prepareModel();
	const auto countMarks = [&](const battle::Unit * unit)
	{
		return unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER, BonusSubtypeID(ScriptID(*marker)))->size();
	};
	const BattleAttackInfo info(shooter, defender, 0, true);
	const auto initialHealth = defender->getAvailableHealth();
	const auto firstDamage = model->battleExpectedLuckDamage(info);
	const auto prediction = AttackPossibility::evaluate(info, shooter->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	ASSERT_EQ(prediction.fortuneStrikes.size(), 2);
	ASSERT_EQ(prediction.fortuneStrikes[0].hits.size(), 1);
	ASSERT_EQ(prediction.fortuneStrikes[1].hits.size(), 1);
	EXPECT_EQ(prediction.fortuneStrikes[0].hits[0].second, firstDamage);
	EXPECT_GT(prediction.fortuneStrikes[1].hits[0].second, firstDamage);
	EXPECT_EQ(countMarks(prediction.effectPreview->getForUpdate(defender->unitId()).get()), 2);
	EXPECT_EQ(countMarks(model->getForUpdate(defender->unitId()).get()), 0);
	EXPECT_EQ(countMarks(defender), 0);
	EXPECT_EQ(defender->getAvailableHealth(), initialHealth);

	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	auto projectedDefender = model->getForUpdate(defender->unitId());
	EXPECT_EQ(countMarks(projectedDefender.get()), 2);
	EXPECT_EQ(projectedDefender->getAvailableHealth(), initialHealth
		- prediction.fortuneStrikes[0].hits[0].second - prediction.fortuneStrikes[1].hits[0].second);
	exchange.trackAttack(model->getForUpdate(shooter->unitId()), projectedDefender,
		true, true, cache, model, true, false);
	EXPECT_EQ(countMarks(projectedDefender.get()), 2); // Read-only target probing.
	exchange.trackAttack(model->getForUpdate(shooter->unitId()), projectedDefender,
		true, true, cache, model, false, false);
	EXPECT_EQ(countMarks(projectedDefender.get()), 3);
	EXPECT_EQ(countMarks(defender), 0);
}

TEST_F(AttackResourceProjectionTest, SorceryRankCapturedMarkValueImprovesDetachedSecondShotProjection)
{
	const auto focus = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		std::string(newHorizonsSorcery::FOCUS_MAGIC_SPELL));
	if(!focus)
		GTEST_SKIP() << "Requires New Horizons Focus Magic content";
	const auto trigger = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
		std::string(newHorizonsSorcery::FOCUS_MAGIC_TRIGGER));
	const auto marker = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
		std::string(newHorizonsSorcery::ARCANE_BREACH_TRIGGER));
	ASSERT_TRUE(trigger);
	ASSERT_TRUE(marker);
	ASSERT_NO_FATAL_FAILURE(prepareCommands());

	constexpr int32_t noRankMarkValue = 1015;
	constexpr int32_t expertMarkValue = 1021;
	constexpr int targetDefense = 2000;
	const auto ignoredDefense = [](int32_t markValue)
	{
		return static_cast<int>(static_cast<int64_t>(markValue) * targetDefense / 10000);
	};
	ASSERT_LT(ignoredDefense(noRankMarkValue), ignoredDefense(expertMarkValue));

	auto * noRankShooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 4), 10);
	auto * expertShooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 6), 10);
	auto * noRankDefender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 4), 1000);
	auto * expertDefender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 6), 1000);
	ASSERT_NE(noRankShooter, nullptr);
	ASSERT_NE(expertShooter, nullptr);
	ASSERT_NE(noRankDefender, nullptr);
	ASSERT_NE(expertDefender, nullptr);

	const auto setPrimarySkill = [](CStack * unit, PrimarySkill skill, int value)
	{
		const int current = skill == PrimarySkill::DEFENSE ? unit->getDefense(true) : unit->getAttack(true);
		Bonus adjustment;
		adjustment.type = BonusType::PRIMARY_SKILL;
		adjustment.subtype = BonusSubtypeID(skill);
		adjustment.val = value - current;
		unit->addNewBonus(std::make_shared<Bonus>(adjustment));
	};
	const int noRankAttack = targetDefense - ignoredDefense(noRankMarkValue);
	for(auto * defender : {noRankDefender, expertDefender})
		setPrimarySkill(defender, PrimarySkill::DEFENSE, targetDefense);
	setPrimarySkill(noRankShooter, PrimarySkill::ATTACK, noRankAttack);
	setPrimarySkill(expertShooter, PrimarySkill::ATTACK, noRankAttack);
	ASSERT_EQ(noRankDefender->getDefense(true), targetDefense);
	ASSERT_EQ(expertDefender->getDefense(true), targetDefense);
	ASSERT_EQ(noRankShooter->getAttack(true), noRankAttack);
	ASSERT_EQ(expertShooter->getAttack(true), noRankAttack);
	forceMaximumDamage(noRankShooter);
	forceMaximumDamage(expertShooter);

	const auto addFocusMagic = [&](CStack * shooter, int32_t markValue)
	{
		auto enchantment = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
			BonusSource::SPELL_EFFECT, markValue, BonusSourceID(SpellID(*focus)), BonusSubtypeID(ScriptID(*trigger)));
		enchantment->turnsRemain = 3;
		JsonNode parameters;
		parameters["beneficiarySide"].Integer() = static_cast<int>(BattleSide::ATTACKER);
		enchantment->parameters = std::make_shared<BonusParameters>(parameters);
		shooter->addNewBonus(enchantment);
	};
	addFocusMagic(noRankShooter, noRankMarkValue);
	addFocusMagic(expertShooter, expertMarkValue);
	prepareModel();
	const auto countMarks = [&](const battle::Unit * unit)
	{
		return unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER,
			BonusSubtypeID(ScriptID(*marker)))->size();
	};

	const auto noRankInitialHealth = noRankDefender->getAvailableHealth();
	const auto expertInitialHealth = expertDefender->getAvailableHealth();
	const auto noRankInitialShots = noRankShooter->shots.available();
	const auto expertInitialShots = expertShooter->shots.available();
	const auto noRankProjectedDefender = model->getForUpdate(noRankDefender->unitId());
	const auto expertProjectedDefender = model->getForUpdate(expertDefender->unitId());
	const auto noRankProjectedShooter = model->getForUpdate(noRankShooter->unitId());
	const auto expertProjectedShooter = model->getForUpdate(expertShooter->unitId());
	const auto noRankProjectedInitialHealth = noRankProjectedDefender->getAvailableHealth();
	const auto expertProjectedInitialHealth = expertProjectedDefender->getAvailableHealth();
	const auto noRankProjectedInitialShots = noRankProjectedShooter->shots.available();
	const auto expertProjectedInitialShots = expertProjectedShooter->shots.available();
	const auto noRankInfo = BattleAttackInfo(noRankShooter, noRankDefender, 0, true);
	const auto expertInfo = BattleAttackInfo(expertShooter, expertDefender, 0, true);
	const auto noRankPrediction = AttackPossibility::evaluate(noRankInfo, noRankShooter->getPosition(), cache, model);
	const auto expertPrediction = AttackPossibility::evaluate(expertInfo, expertShooter->getPosition(), cache, model);
	ASSERT_NE(noRankPrediction.effectPreview, nullptr);
	ASSERT_NE(expertPrediction.effectPreview, nullptr);
	ASSERT_EQ(noRankPrediction.fortuneStrikes.size(), 2);
	ASSERT_EQ(expertPrediction.fortuneStrikes.size(), 2);
	ASSERT_FALSE(noRankPrediction.fortuneStrikes[0].hits.empty());
	ASSERT_FALSE(expertPrediction.fortuneStrikes[0].hits.empty());
	ASSERT_EQ(noRankPrediction.fortuneStrikes[1].hits.size(), 1);
	ASSERT_EQ(expertPrediction.fortuneStrikes[1].hits.size(), 1);
	const auto noRankFirstDamage = noRankPrediction.fortuneStrikes[0].hits[0].second;
	const auto expertFirstDamage = expertPrediction.fortuneStrikes[0].hits[0].second;
	EXPECT_EQ(noRankFirstDamage, expertFirstDamage);
	EXPECT_EQ(noRankFirstDamage, model->battleExpectedLuckDamage(noRankInfo));
	EXPECT_EQ(expertFirstDamage, model->battleExpectedLuckDamage(expertInfo));
	EXPECT_GT(expertPrediction.fortuneStrikes[1].hits[0].second,
		noRankPrediction.fortuneStrikes[1].hits[0].second);

	const auto noRankPreviewMarks = noRankPrediction.effectPreview->getForUpdate(noRankDefender->unitId())
		->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER, BonusSubtypeID(ScriptID(*marker)));
	const auto expertPreviewMarks = expertPrediction.effectPreview->getForUpdate(expertDefender->unitId())
		->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER, BonusSubtypeID(ScriptID(*marker)));
	ASSERT_EQ(noRankPreviewMarks->size(), 2);
	ASSERT_EQ(expertPreviewMarks->size(), 2);
	EXPECT_EQ(noRankPreviewMarks->front()->val, noRankMarkValue);
	EXPECT_EQ(expertPreviewMarks->front()->val, expertMarkValue);
	EXPECT_EQ(countMarks(model->getForUpdate(noRankDefender->unitId()).get()), 0);
	EXPECT_EQ(countMarks(model->getForUpdate(expertDefender->unitId()).get()), 0);
	EXPECT_EQ(countMarks(noRankDefender), 0);
	EXPECT_EQ(countMarks(expertDefender), 0);
	EXPECT_EQ(noRankDefender->getAvailableHealth(), noRankInitialHealth);
	EXPECT_EQ(expertDefender->getAvailableHealth(), expertInitialHealth);
	EXPECT_EQ(noRankShooter->shots.available(), noRankInitialShots);
	EXPECT_EQ(expertShooter->shots.available(), expertInitialShots);
	EXPECT_EQ(noRankProjectedDefender->getAvailableHealth(), noRankProjectedInitialHealth);
	EXPECT_EQ(expertProjectedDefender->getAvailableHealth(), expertProjectedInitialHealth);
	EXPECT_EQ(noRankProjectedShooter->shots.available(), noRankProjectedInitialShots);
	EXPECT_EQ(expertProjectedShooter->shots.available(), expertProjectedInitialShots);

	BattleExchangeVariant exchange;
	exchange.trackAttack(noRankPrediction, model, cache);
	exchange.trackAttack(expertPrediction, model, cache);
	EXPECT_EQ(countMarks(noRankProjectedDefender.get()), 2);
	EXPECT_EQ(countMarks(expertProjectedDefender.get()), 2);
	EXPECT_LT(expertProjectedDefender->getAvailableHealth(), noRankProjectedDefender->getAvailableHealth());
	EXPECT_EQ(countMarks(noRankDefender), 0);
	EXPECT_EQ(countMarks(expertDefender), 0);
	EXPECT_EQ(noRankDefender->getAvailableHealth(), noRankInitialHealth);
	EXPECT_EQ(expertDefender->getAvailableHealth(), expertInitialHealth);
	EXPECT_EQ(noRankShooter->shots.available(), noRankInitialShots);
	EXPECT_EQ(expertShooter->shots.available(), expertInitialShots);
}

TEST_F(AttackResourceProjectionTest, ArcaneAcquisitionProjectsTwoInitialMarksAndOneSubsequentMark)
{
	const auto focus = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		std::string(newHorizonsSorcery::FOCUS_MAGIC_SPELL));
	if(!focus)
		GTEST_SKIP() << "Requires New Horizons Focus Magic content";
	const auto trigger = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
		std::string(newHorizonsSorcery::FOCUS_MAGIC_TRIGGER));
	const auto marker = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
		std::string(newHorizonsSorcery::ARCANE_BREACH_TRIGGER));
	ASSERT_TRUE(trigger);
	ASSERT_TRUE(marker);
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	forceMaximumDamage(shooter);
	auto enchantment = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
		BonusSource::SPELL_EFFECT, 2000, BonusSourceID(SpellID(*focus)), BonusSubtypeID(ScriptID(*trigger)));
	enchantment->turnsRemain = 3;
	JsonNode parameters;
	parameters["beneficiarySide"].Integer() = static_cast<int>(BattleSide::ATTACKER);
	parameters["arcaneAcquisition"].Bool() = true;
	enchantment->parameters = std::make_shared<BonusParameters>(parameters);
	shooter->addNewBonus(enchantment);
	prepareModel();
	const auto countMarks = [&](const battle::Unit * unit)
	{
		return unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER,
			BonusSubtypeID(ScriptID(*marker)))->size();
	};
	const BattleAttackInfo info(shooter, defender, 0, true);
	const auto initialHealth = defender->getAvailableHealth();
	const auto firstDamage = model->battleExpectedLuckDamage(info);
	const auto prediction = AttackPossibility::evaluate(info, shooter->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	ASSERT_EQ(prediction.fortuneStrikes.size(), 1);
	ASSERT_EQ(prediction.fortuneStrikes[0].hits.size(), 1);
	EXPECT_EQ(prediction.fortuneStrikes[0].hits[0].second, firstDamage);
	EXPECT_EQ(countMarks(prediction.effectPreview->getForUpdate(defender->unitId()).get()), 2);
	EXPECT_EQ(countMarks(model->getForUpdate(defender->unitId()).get()), 0);
	EXPECT_EQ(countMarks(defender), 0);
	EXPECT_EQ(defender->getAvailableHealth(), initialHealth);

	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	auto projectedDefender = model->getForUpdate(defender->unitId());
	auto projectedShooter = model->getForUpdate(shooter->unitId());
	ASSERT_EQ(countMarks(projectedDefender.get()), 2);
	EXPECT_EQ(projectedDefender->getAvailableHealth(), initialHealth - prediction.fortuneStrikes[0].hits[0].second);

	const BattleAttackInfo secondInfo(projectedShooter.get(), projectedDefender.get(), 0, true);
	const auto secondPrediction = AttackPossibility::evaluate(
		secondInfo, projectedShooter->getPosition(), cache, model);
	ASSERT_NE(secondPrediction.effectPreview, nullptr);
	ASSERT_EQ(secondPrediction.fortuneStrikes.size(), 1);
	ASSERT_EQ(secondPrediction.fortuneStrikes[0].hits.size(), 1);
	EXPECT_GT(secondPrediction.fortuneStrikes[0].hits[0].second, firstDamage);
	EXPECT_EQ(countMarks(secondPrediction.effectPreview->getForUpdate(defender->unitId()).get()), 3);
	EXPECT_EQ(countMarks(projectedDefender.get()), 2);
	exchange.trackAttack(secondPrediction, model, cache);
	EXPECT_EQ(countMarks(projectedDefender.get()), 3);
	EXPECT_EQ(countMarks(defender), 0);
}

TEST_F(AttackResourceProjectionTest, RangedMarkCacheDropsPenetrationAfterExpiry)
{
	const auto spell = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell",
		std::string(newHorizonsSorcery::ARCANE_BREACH_EFFECT));
	if(!spell)
		GTEST_SKIP() << "Requires New Horizons Arcane Breach content";
	const auto trigger = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
		std::string(newHorizonsSorcery::ARCANE_BREACH_TRIGGER));
	ASSERT_TRUE(trigger);
	// Exercise mark invalidation without the independent Focus Fire cache bypass.
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	forceMaximumDamage(shooter);
	prepareModel();
	auto projectedShooter = model->getForUpdate(shooter->unitId());
	auto projectedDefender = model->getForUpdate(defender->unitId());
	const auto baseline = model->battleExpectedLuckDamage(
		BattleAttackInfo(projectedShooter.get(), projectedDefender.get(), 0, true));
	Bonus mark(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
		BonusSource::SPELL_EFFECT, 2000, BonusSourceID(SpellID(*spell)), BonusSubtypeID(ScriptID(*trigger)));
	mark.turnsRemain = 2;
	JsonNode parameters;
	parameters["beneficiarySide"].Integer() = static_cast<int>(BattleSide::ATTACKER);
	mark.parameters = std::make_shared<BonusParameters>(parameters);
	model->addUnitBonus(defender->unitId(), {mark});
	DamageCache markedCache;
	markedCache.cacheDamage(projectedShooter.get(), projectedDefender.get(), model);
	ASSERT_GT(markedCache.getDamage(projectedShooter.get(), projectedDefender.get(), model), baseline);
	model->nextRound();
	model->nextRound();
	ASSERT_TRUE(projectedDefender->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER,
		BonusSubtypeID(ScriptID(*trigger)))->empty());
	EXPECT_EQ(markedCache.getDamage(projectedShooter.get(), projectedDefender.get(), model), baseline);
	DamageCache childCache(&markedCache);
	EXPECT_EQ(childCache.getDamage(projectedShooter.get(), projectedDefender.get(), model), baseline);
	EXPECT_TRUE(defender->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER,
		BonusSubtypeID(ScriptID(*trigger)))->empty());
}

TEST_F(AttackResourceProjectionTest, TrackingFirstOfTwoRetaliationsConsumesItWhileRetaliationRemainsPossible)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(7, 5), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(8, 5), 100);
	Bonus extra;
	extra.type = BonusType::ADDITIONAL_RETALIATION;
	extra.val = 1;
	defender->addNewBonus(std::make_shared<Bonus>(extra));
	ASSERT_EQ(defender->counterAttacks.available(), 2);
	prepareModel();
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(attacker, defender, 0, false),
		attacker->getPosition(), cache, model);
	const auto affected = std::find_if(attack.affectedUnits.begin(), attack.affectedUnits.end(),
		[&](const auto & unit){ return unit->unitId() == defender->unitId(); });
	ASSERT_NE(affected, attack.affectedUnits.end());
	ASSERT_TRUE((*affected)->alive());
	ASSERT_EQ((*affected)->counterAttacks.available(), 1);
	ASSERT_TRUE((*affected)->ableToRetaliate());
	BattleExchangeVariant exchange;
	exchange.trackAttack(attack, model, cache);
	EXPECT_EQ(model->getForUpdate(defender->unitId())->counterAttacks.available(), 1);
	EXPECT_TRUE(model->battleGetUnitByID(defender->unitId())->ableToRetaliate());
	EXPECT_EQ(defender->counterAttacks.available(), 2);
	model->nextRound();
	EXPECT_EQ(model->getForUpdate(defender->unitId())->counterAttacks.available(), 2);
}
