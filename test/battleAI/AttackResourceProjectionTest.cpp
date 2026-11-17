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
#include "../../AI/BattleAI/FocusMagicProjection.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
#include <atomic>
#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
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
#include "../../AI/BattleAI/HypotheticBattle.h"

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

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(!useCommands)
			captureLegacyPerkOfferPolicy(*map);
	}

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
	const auto * projectedOpening = model->battleGetUnitByID(opening->unitId());
	const auto * projectedFollowup = model->battleGetUnitByID(followup->unitId());
	const auto * projectedEnemyOgre = model->battleGetUnitByID(enemyOgre->unitId());
	ASSERT_NE(projectedOpening, nullptr);
	ASSERT_NE(projectedFollowup, nullptr);
	ASSERT_NE(projectedEnemyOgre, nullptr);
	ASSERT_NE(projectedOpening, opening);
	ASSERT_TRUE(model->battleCanShoot(projectedOpening));
	ASSERT_TRUE(model->battleCanShoot(projectedFollowup));
	ASSERT_FALSE(model->battleCanShoot(projectedEnemyOgre));

	const auto openingShots = opening->shots.available();
	const auto followupShots = followup->shots.available();
	const auto enemyShots = enemyOgre->shots.available();
	ASSERT_EQ(enemyShots, 0);
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(projectedOpening, projectedEnemyOgre, 0, true),
		projectedOpening->getPosition(), cache, model);
	ASSERT_NE(attack.attackerState, nullptr);
	PotentialTargets targets(projectedOpening, cache, model);
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
	ASSERT_TRUE(isScheduled(projectedOpening));
	ASSERT_TRUE(isScheduled(projectedFollowup));
	ASSERT_TRUE(isScheduled(projectedEnemyOgre));
	ASSERT_TRUE(vstd::contains(units.melleeAccessible, projectedOpening));
	ASSERT_TRUE(vstd::contains(units.melleeAccessible, projectedFollowup));
	ASSERT_FALSE(vstd::contains(units.shooters, projectedOpening));
	ASSERT_FALSE(vstd::contains(units.shooters, projectedFollowup));

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

TEST_F(AttackResourceProjectionTest, FutureActorKilledAtActivationCannotContributeAnOrdinaryAttack)
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
	for(const auto [unit, speed] : {std::pair{opening, 20}, {followup, 10}})
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::STACKS_SPEED, BonusSource::OTHER, speed, BonusSourceID()));
	defender->movedThisRound = true;
	// Existing poison deliberately exercises the correctness guard without
	// enabling the separately held burn nextTurn routing.
	followup->physicalPoisonBaseDamage = followup->getAvailableHealth();
	followup->physicalPoisonActivationsRemaining = 3;
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = opening->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	prepareModel();
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(opening, defender, 0, false),
		opening->getPosition(), cache, model);
	PotentialTargets targets(opening, cache, model);
	BattleExchangeEvaluator evaluator(callback, environment, 1.0f, 1);
	evaluator.updateReachabilityMap(model);
	const auto units = evaluator.getExchangeUnits(attack, 0, targets, model);
	ASSERT_EQ(units.units.at(0).size(), 2u);
	ASSERT_EQ(units.units.at(0).at(1)->unitId(), followup->unitId());
	const auto actual = evaluator.evaluateExchange(attack, 0, targets, cache, model);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), callback);
	BattleExchangeVariant expected;
	expected.trackAttack(attack, reference, cache);
	reference->nextTurn(followup->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_FALSE(reference->battleGetUnitByID(followup->unitId())->alive());
	const auto expectedScore = (expected.getScore().enemyDamageReduce - expected.getScore().ourDamageReduce) / 2.0f;
	EXPECT_FLOAT_EQ(actual, expectedScore);
	EXPECT_TRUE(followup->alive());
	EXPECT_EQ(followup->physicalPoisonActivationsRemaining, 3);
}

TEST_F(AttackResourceProjectionTest, FocusDoubleShotConsumesTwoHardChargesWithoutChangingSource)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	forceMaximumDamage(shooter);
	const auto focus = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
	ASSERT_TRUE(focus.hasValue());
	for(const auto & bonus : newHorizonsFocusMagic::enchantment(*shooter, focus))
		shooter->addNewBonus(std::make_shared<Bonus>(bonus));
	prepareModel();
	const auto sourceShots = shooter->shots.available();
	const auto sourceHealth = defender->getAvailableHealth();
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(shooter, defender, 0, true),
		shooter->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*prediction.attackerState), 1);
	EXPECT_EQ(prediction.attackerState->shots.available(), sourceShots - 2);
	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*model->getForUpdate(shooter->unitId())), 1);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*shooter), 3);
	EXPECT_EQ(shooter->shots.available(), sourceShots);
	EXPECT_EQ(defender->getAvailableHealth(), sourceHealth);
}

TEST_F(AttackResourceProjectionTest, FocusCacheDropsEnhancementAfterThirdChargeAndKeepsParentCacheSafe)
{
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 5), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 1000);
	forceMaximumDamage(shooter);
	prepareModel();
	auto projected = model->getForUpdate(shooter->unitId());
	const auto * target = model->getForUpdate(defender->unitId()).get();
	const auto baseline = model->battleExpectedLuckDamage(BattleAttackInfo(projected.get(), target, 0, true));
	const auto focus = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
	ASSERT_TRUE(focus.hasValue());
	projected->addUnitBonus(newHorizonsFocusMagic::enchantment(*projected, focus));
	DamageCache enhanced;
	enhanced.cacheDamage(projected.get(), target, model);
	EXPECT_GT(enhanced.getDamage(projected.get(), target, model), baseline);
	for(int strike = 0; strike < 3; ++strike) focusMagicAI::afterAttack(*projected, true, false);
	EXPECT_FALSE(newHorizonsFocusMagic::active(*projected));
	EXPECT_EQ(enhanced.getDamage(projected.get(), target, model), baseline);
	DamageCache childCache(&enhanced);
	EXPECT_EQ(childCache.getDamage(projected.get(), target, model), baseline);
	EXPECT_FALSE(newHorizonsFocusMagic::active(*shooter));
}
