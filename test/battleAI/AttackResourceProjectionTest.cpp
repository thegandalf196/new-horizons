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
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"

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
