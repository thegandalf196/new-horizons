/*
 * DamageReductionControlTest.cpp, part of VCMI engine
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
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"

namespace
{
class DamageEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DamageEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class DamageReductionControlTest : public HeroCommandFixture {};

TEST_F(DamageReductionControlTest, ImplicitDamageMeasurementUsesAnOpponentOfTheCurrentController)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * own = addStack(BattleSide::ATTACKER, creatureByName("core:stoneGolem"), BattleHex(3, 5), 100);
	auto * subject = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(8, 5), 100);
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != own && unit != subject && unit != other)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto environment = std::make_shared<DamageEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto measure = [&](const battle::Unit * opponent)
	{
		return AttackPossibility::calculateDamageReduce(opponent, subject, 5, cache, model);
	};
	const auto againstOwn = measure(own);
	const auto againstOther = measure(other);
	ASSERT_NE(againstOwn, againstOther); // Distinct defenses make the wrong peer observable.
	EXPECT_FLOAT_EQ(measure(nullptr), againstOther);
	auto control = std::make_shared<Bonus>();
	control->type = BonusType::HYPNOTIZED;
	control->duration = BonusDuration::ONE_BATTLE;
	subject->addNewBonus(control);
	ASSERT_EQ(model->battleGetOwner(subject), PlayerColor(1));
	EXPECT_FLOAT_EQ(measure(nullptr), againstOwn);
	subject->removeBonus(control);
	ASSERT_EQ(model->battleGetOwner(subject), PlayerColor(0));
	EXPECT_FLOAT_EQ(measure(nullptr), againstOther);
	EXPECT_EQ(subject->getAvailableHealth(), subject->getTotalHealth());
}

TEST_F(DamageReductionControlTest, PhantomArmyIntegrityOnlyRemovesOffensiveCountWhenLethal)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	auto * phantom = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(3, 5), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(4, 5), 100);
	phantom->summoned = true;
	phantom->initializePhantomProfile(100, 2);
	ASSERT_EQ(phantom->getCount(), 100);
	ASSERT_EQ(phantom->getPhantomIntegrity(), 100);

	auto environment = std::make_shared<DamageEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	const auto * phantomModel = model->battleGetUnitByID(phantom->unitId());
	const auto * enemyModel = model->battleGetUnitByID(enemy->unitId());
	ASSERT_NE(phantomModel, nullptr);
	ASSERT_NE(enemyModel, nullptr);
	ASSERT_EQ(phantomModel->getCount(), 100);

	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto fullCopiedStackDamage = cache.getOriginalDamage(phantomModel, enemyModel, model);
	ASSERT_GT(fullCopiedStackDamage, 0);

	const auto partialHitValue = AttackPossibility::calculateDamageReduce(
		enemyModel, phantomModel, 99, cache, model);
	EXPECT_FLOAT_EQ(partialHitValue, 0.0f);
	EXPECT_EQ(phantomModel->getCount(), 100);
	const auto lethalHitValue = AttackPossibility::calculateDamageReduce(
		enemyModel, phantomModel, 100, cache, model);
	EXPECT_FLOAT_EQ(lethalHitValue, static_cast<float>(fullCopiedStackDamage));
	EXPECT_EQ(phantomModel->getCount(), 100);
	EXPECT_EQ(phantomModel->getPhantomIntegrity(), 100);
}

TEST_F(DamageReductionControlTest, HypotheticPhantomKeepsOpeningDurationAndExpiresWithAuthoritativeBattle)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	auto * phantom = addStack(BattleSide::ATTACKER, creatureByName("core:ogre"), BattleHex(3, 5), 100);
	ASSERT_NE(phantom, nullptr);
	phantom->summoned = true;
	phantom->initializePhantomProfile(100, 2);
	ASSERT_EQ(battle()->battleGetRound(), 0);

	auto environment = std::make_shared<DamageEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), callback);
	const auto phantomId = phantom->unitId();

	// The opening transition grants the full duration. One later round transition
	// leaves one round; the following transition expires the phantom in both states.
	for(int transition = 0; transition < 2; ++transition)
	{
		model.nextRound();
		battle()->nextRound();
		const auto * phantomModel = model.battleGetUnitByID(phantomId);
		ASSERT_NE(phantomModel, nullptr);
		EXPECT_TRUE(phantomModel->alive());
		EXPECT_TRUE(phantom->alive());
		EXPECT_FALSE(phantom->ghostPending);
	}

	model.nextRound();
	battle()->nextRound();
	const auto * expiredModel = model.battleGetUnitByID(phantomId);
	ASSERT_NE(expiredModel, nullptr);
	EXPECT_FALSE(expiredModel->alive());
	EXPECT_TRUE(expiredModel->isGhost());
	EXPECT_FALSE(expiredModel->isValidTarget());
	EXPECT_FALSE(phantom->alive());
	EXPECT_TRUE(phantom->ghostPending);
	EXPECT_EQ(phantom->getPhantomIntegrity(), 0);
}
