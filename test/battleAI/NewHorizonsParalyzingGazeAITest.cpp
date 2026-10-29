/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/NewHorizonsParalyzingGazeFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/ParalyzingGazeProjection.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include <tuple>
namespace
{
class GazeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GazeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
	std::shared_ptr<HypotheticBattle> model(CBattleInfoCallback * source)
	{
		return std::make_shared<HypotheticBattle>(this,
			std::shared_ptr<CBattleInfoCallback>(source, [](CBattleInfoCallback *) {}));
	}
};
class NewHorizonsParalyzingGazeAITest : public NewHorizonsParalyzingGazeBattleTest {};
}

TEST_F(NewHorizonsParalyzingGazeAITest, MeleePreviewSelectedFutureAndNativeConsumeExactlyOnePendingToken)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(target);
	const auto rngBytes = [&]()
	{
		CMemorySerializer authority, fixture, defaults;
		authority.oser & *gameHandler->randomizer;
		fixture.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		defaults.oser & CRandomGenerator::getDefault();
		return std::tuple{authority.extractBuffer(), fixture.extractBuffer(), defaults.extractBuffer()};
	};
	const auto rngBefore = rngBytes();
	GazeEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(melee, target, 0, false), melee->getPosition(), cache, parent);
	ASSERT_NE(ap.effectPreview, nullptr);
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(ap, selected, cache);
	auto future = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant forecast;
	forecast.trackAttack(future->getForUpdate(melee->unitId()), future->getForUpdate(target->unitId()), false, true, cache, future);
	for(const auto & branch : {ap.effectPreview, selected, future})
	{
		EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*branch->battleGetUnitByID(target->unitId())));
		EXPECT_FALSE(branch->battleGetUnitByID(target->unitId())->ableToRetaliate());
		EXPECT_EQ(branch->battleGetUnitByID(melee->unitId())->getAvailableHealth(), melee->getAvailableHealth());
	}
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*parent->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*sibling->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(rngBytes(), rngBefore);
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_EQ(melee->getAvailableHealth(), selected->battleGetUnitByID(melee->unitId())->getAvailableHealth());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
}
TEST_F(NewHorizonsParalyzingGazeAITest, ApplicationProjectsThroughShotPreviewSelectedAndFutureWithoutParentMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	GazeEnvironment environment(gameState());
	auto parent = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, true), source->getPosition(), cache, parent);
	ASSERT_NE(ap.effectPreview, nullptr);
	ASSERT_TRUE(newHorizonsParalyzingGaze::pending(*ap.effectPreview->battleGetUnitByID(target->unitId())));
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(ap, selected, cache);
	auto future = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant forecast;
	forecast.trackAttack(future->getForUpdate(source->unitId()), future->getForUpdate(target->unitId()), true, true, cache, future);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*selected->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*future->battleGetUnitByID(target->unitId())));
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*parent->battleGetUnitByID(target->unitId())));
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
}
TEST_F(NewHorizonsParalyzingGazeAITest, DefiantPreviewSelectedAndFutureSpendOnlyTheirOwnBudgetAndAllowCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	GazeEnvironment environment(gameState());
	auto parent = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(melee, target, 0, false), melee->getPosition(), cache, parent);
	ASSERT_NE(ap.effectPreview, nullptr);
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(ap, selected, cache);
	auto future = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant forecast;
	forecast.trackAttack(future->getForUpdate(melee->unitId()), future->getForUpdate(target->unitId()), false, true, cache, future);
	for(const auto & branch : {ap.effectPreview, selected, future})
	{
		EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*branch->battleGetUnitByID(target->unitId())));
		EXPECT_EQ(branch->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
		EXPECT_LT(branch->battleGetUnitByID(melee->unitId())->getAvailableHealth(), melee->getAvailableHealth());
	}
	EXPECT_EQ(parent->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
}
TEST_F(NewHorizonsParalyzingGazeAITest, ExistingRangedRetaliationForecastOmissionIsExplicitWithoutInventedGazeStrike)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->removeUnit(target->unitId());
	target = addStack(BattleSide::DEFENDER, creatureByName("core:archMage"), BattleHex(12, 5), 100);
	give(target, BonusType::RANGED_RETALIATION);
	battle()->moveUnit(melee->unitId(), BattleHex(3, 8));
	ASSERT_TRUE(battle()->battleCanShoot(target, source->getPosition()));
	GazeEnvironment environment(gameState());
	auto parent = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, true), source->getPosition(), cache, parent);
	ASSERT_NE(ap.effectPreview, nullptr);
	// OPEN shared Phase2 gap: native immediately consumes this token/counter;
	// AP has no generic ranged retaliation branch. Do not claim native parity.
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*ap.effectPreview->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(ap.effectPreview->battleGetUnitByID(target->unitId())->ableToRetaliate());
}

TEST_F(NewHorizonsParalyzingGazeAITest, HistoricalNoGazeMeleeKeepsExistingCounterDamageAndResult)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	GazeEnvironment environment(gameState());
	auto parent = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(melee, target, 0, false), melee->getPosition(), cache, parent);
	ASSERT_NE(ap.attackerState, nullptr);
	EXPECT_LT(ap.attackerState->getAvailableHealth(), melee->getAvailableHealth());
	EXPECT_FALSE(paralyzingGazeAI::needsProjection(*parent, BattleAttackInfo(melee, target, 0, false)));
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(ap, selected, cache);
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*selected->battleGetUnitByID(target->unitId())));
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_EQ(melee->getAvailableHealth(), selected->battleGetUnitByID(melee->unitId())->getAvailableHealth());
}

TEST_F(NewHorizonsParalyzingGazeAITest, StaleReplayRejectsBeforeBranchParentSiblingOrRNGMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	GazeEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto snapshot = [&](const std::shared_ptr<HypotheticBattle> & model)
	{
		std::vector<JsonNode> units, tokens;
		for(const auto * live : {source, melee, target})
		{
			const auto unit = model->getForUpdate(live->unitId());
			units.push_back(unit->save());
			const auto pending = newHorizonsParalyzingGaze::pending(*unit);
			tokens.push_back(pending ? pending->toJsonNode() : JsonNode());
		}
		return std::tuple{units, tokens, model->getArmorerDefiantState(BattleSide::ATTACKER),
			model->getArmorerDefiantState(BattleSide::DEFENDER)};
	};
	const auto rngBytes = [&]()
	{
		CMemorySerializer authority, fixture, defaults;
		authority.oser & *gameHandler->randomizer;
		fixture.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		defaults.oser & CRandomGenerator::getDefault();
		return std::tuple{authority.extractBuffer(), fixture.extractBuffer(), defaults.extractBuffer()};
	};
	ASSERT_TRUE(branch->battleCanUseGazeDefiant(branch->battleGetUnitByID(melee->unitId()),
		branch->battleGetUnitByID(target->unitId()), false));
	const auto branchBefore = snapshot(branch);
	const auto parentBefore = snapshot(parent);
	const auto siblingBefore = snapshot(sibling);
	const auto nativeBefore = target->save();
	const auto nativeBudget = battle()->getArmorerDefiantState(BattleSide::DEFENDER);
	const auto rngBefore = rngBytes();
	const paralyzingGazeAI::Consumption stale{melee->unitId(), target->unitId(), false, false};
	EXPECT_THROW(paralyzingGazeAI::replay(*branch, stale), std::runtime_error);
	EXPECT_EQ(snapshot(branch), branchBefore);
	EXPECT_EQ(snapshot(parent), parentBefore);
	EXPECT_EQ(snapshot(sibling), siblingBefore);
	EXPECT_EQ(target->save(), nativeBefore);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER), nativeBudget);
	EXPECT_EQ(rngBytes(), rngBefore);

	// The same preflight accepts a current receipt and permits the real counter
	// without spending its counter resource when Defiant ignores the token.
	auto positive = std::make_shared<HypotheticBattle>(&environment, parent);
	const paralyzingGazeAI::Consumption current{melee->unitId(), target->unitId(), false, true};
	EXPECT_NO_THROW(paralyzingGazeAI::replay(*positive, current));
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*positive->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(positive->battleGetUnitByID(target->unitId())->ableToRetaliate());
	EXPECT_EQ(positive->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
	EXPECT_EQ(snapshot(parent), parentBefore);
	EXPECT_EQ(snapshot(sibling), siblingBefore);
	EXPECT_EQ(rngBytes(), rngBefore);

	// A dead incoming foe makes the opportunity stale even when the recorded
	// no-Defiant disposition agrees with the current (ineligible) query.
	auto deadOpponent = branch->getForUpdate(melee->unitId());
	int64_t lethal = deadOpponent->getAvailableHealth();
	deadOpponent->damage(lethal);
	ASSERT_FALSE(deadOpponent->alive());
	ASSERT_FALSE(branch->battleCanUseGazeDefiant(deadOpponent.get(),
		branch->battleGetUnitByID(target->unitId()), false));
	const auto deadBranchBefore = snapshot(branch);
	EXPECT_THROW(paralyzingGazeAI::replay(*branch, stale), std::runtime_error);
	EXPECT_EQ(snapshot(branch), deadBranchBefore);
	EXPECT_EQ(snapshot(parent), parentBefore);
	EXPECT_EQ(snapshot(sibling), siblingBefore);
	EXPECT_EQ(target->save(), nativeBefore);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER), nativeBudget);
	EXPECT_EQ(rngBytes(), rngBefore);
}
