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
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/bonuses/BonusParameters.h"
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
