/*
 * NewHorizonsWispLongReachAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleUnitTurnReason.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
class WispEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WispEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class WispCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> actions;
	WispCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { actions.push_back(action); }
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
};
}

class NewHorizonsWispLongReachAITest : public HeroCommandFixture
{
protected:
	CStack * wisp = nullptr;
	CStack * target = nullptr;
	std::shared_ptr<WispEnvironment> environment;
	std::shared_ptr<WispCallback> callback;

	void prepare(const std::string & attacker = "new-horizons:wisp", bool movement = false,
		BattleHex from = BattleHex(5, 4), BattleHex to = BattleHex(11, 4),
		const std::string & defender = "core:peasant", bool blocked = false)
	{
		startGame();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		wisp = addStack(BattleSide::ATTACKER, creatureByName(attacker), from, 30);
		target = addStack(BattleSide::DEFENDER, creatureByName(defender), to, 1000);
		if(!movement)
			wisp->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
				BonusSource::CREATURE_ABILITY, -100, BonusSourceID(wisp->creatureId())));
		if(blocked)
			addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), BattleHex(8, 4), 1);
		beginCombat();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = wisp->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
		callback = std::make_shared<WispCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<WispEnvironment>(gameState());
	}

	PotentialTargets forecast(std::shared_ptr<HypotheticBattle> & state, DamageCache & cache)
	{
		state = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		cache.buildDamageCache(state, BattleSide::ATTACKER);
		return PotentialTargets(state->battleGetUnitByID(wisp->unitId()), cache, state);
	}

	void runAI()
	{
		CBattleAI ai;
		ai.initBattleInterface(environment, callback);
		ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
			attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
		ai.activeStack(BattleID(0), wisp);
	}

	void acceptDistantAttack(bool moved)
	{
		const auto start = wisp->getPosition();
		const auto before = target->getAvailableHealth();
		runAI();
		ASSERT_EQ(callback->actions.size(), 1u);
		const auto & action = callback->actions.front();
		ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
		ASSERT_EQ(action.target.size(), 2u);
		const auto attackFrom = action.target.front().hexValue;
		EXPECT_EQ(attackFrom != start, moved);
		EXPECT_FALSE(battle()->isMeleeAttackPossible(wisp, target, attackFrom));
		ASSERT_TRUE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target, attackFrom));
		const auto roundBefore = battle()->getRound();
		const auto serialBefore = battle()->getActivationSerial();
		const auto activationsBefore = server.stackActivations.size();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_LT(target->getAvailableHealth(), before);
		EXPECT_EQ(wisp->getPosition(), attackFrom);
		const auto * active = battle()->battleActiveUnit();
		if(active && active->unitId() == wisp->unitId())
		{
			// A last waited attack may finish the round and activate this same
			// stack in the next round; positive Morale may also grant a new turn.
			// Neither means the accepted attack left its original activation open.
			ASSERT_GT(server.stackActivations.size(), activationsBefore);
			const auto & activation = server.stackActivations.back();
			EXPECT_EQ(activation.stack, wisp->unitId());
			EXPECT_GT(battle()->getActivationSerial(), serialBefore);
			EXPECT_TRUE(activation.reason == BattleUnitTurnReason::MORALE
				|| (activation.reason == BattleUnitTurnReason::TURN_QUEUE && battle()->getRound() > roundBefore))
				<< "Same-unit activation reason=" << static_cast<int>(activation.reason)
				<< ", round " << roundBefore << " -> " << battle()->getRound();
		}
	}
};

TEST_F(NewHorizonsWispLongReachAITest, StationaryDistantForecastIsPositiveIsolatedAndActualAIRequestAccepted)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(wisp->getMovementRange(0), 0u);
	ASSERT_FALSE(battle()->isMeleeAttackPossible(wisp, target));
	ASSERT_TRUE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target));
	const auto beforeWisp = wisp->save();
	const auto beforeTarget = target->save();
	std::shared_ptr<HypotheticBattle> state;
	DamageCache cache;
	const auto options = forecast(state, cache);
	ASSERT_FALSE(options.possibleAttacks.empty());
	EXPECT_GT(options.bestActionValue(), 0);
	EXPECT_EQ(options.bestAction().from, wisp->getPosition());
	EXPECT_EQ(options.bestAction().dest, target->getPosition());
	EXPECT_FALSE(options.bestAction().affectedUnits.empty());
	EXPECT_EQ(wisp->save(), beforeWisp);
	EXPECT_EQ(target->save(), beforeTarget);
	acceptDistantAttack(false);
}

TEST_F(NewHorizonsWispLongReachAITest, MovementPlusDistantPhysicalAttackUsesNormalMovementBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisp", true, BattleHex(3, 4), BattleHex(14, 4)));
	ASSERT_FALSE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target));
	ASSERT_EQ(wisp->getMovementRange(0), 5u);
	std::shared_ptr<HypotheticBattle> state;
	DamageCache cache;
	const auto options = forecast(state, cache);
	ASSERT_FALSE(options.possibleAttacks.empty());
	EXPECT_GT(options.bestActionValue(), 0);
	EXPECT_NE(options.bestAction().from, wisp->getPosition());
	EXPECT_LE(BattleHex::getDistance(wisp->getPosition(), options.bestAction().from), 5);
	EXPECT_FALSE(battle()->isMeleeAttackPossible(wisp, target, options.bestAction().from));
	EXPECT_TRUE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target, options.bestAction().from));
	// Existing exchange policy prefers delaying this attack. Keep that policy
	// and exercise the real deferred activation rather than tuning its score.
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	ASSERT_EQ(callback->actions.front().actionType, EActionType::WAIT);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->actions.front()));
	ASSERT_EQ(battle()->battleActiveUnit(), target);
	const auto targetPosition = target->getPosition();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeDefend(target)));
	ASSERT_EQ(battle()->battleActiveUnit(), wisp);
	ASSERT_TRUE(wisp->acquireState()->waitedThisTurn);
	ASSERT_EQ(target->getPosition(), targetPosition);
	callback->actions.clear();
	acceptDistantAttack(true);
}

TEST_F(NewHorizonsWispLongReachAITest, DoubleWideTargetUsesSharedClosestFootprintAndNormalTargetAnchor)
{
	// Defender-side double-wide creatures occupy the cell to their right.
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisp", false, BattleHex(12, 4), BattleHex(5, 4), "core:cavalier"));
	ASSERT_EQ(BattleHex::getDistance(wisp->getPosition(), target->getPosition()), 7);
	ASSERT_EQ(BattleHex::getDistance(wisp->getPosition(), target->occupiedHex()), 6);
	ASSERT_TRUE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target));
	acceptDistantAttack(false);
}

TEST_F(NewHorizonsWispLongReachAITest, OccupiedDirectCorridorDoesNotCreateDistantForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisp", false, BattleHex(5, 4), BattleHex(11, 4), "core:peasant", true));
	ASSERT_FALSE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target));
	std::shared_ptr<HypotheticBattle> state;
	DamageCache cache;
	EXPECT_TRUE(forecast(state, cache).possibleAttacks.empty());
}

TEST_F(NewHorizonsWispLongReachAITest, BeyondRangeDoesNotCreateDistantForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wisp", false, BattleHex(5, 4), BattleHex(12, 4)));
	ASSERT_FALSE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target));
	std::shared_ptr<HypotheticBattle> state;
	DamageCache cache;
	EXPECT_TRUE(forecast(state, cache).possibleAttacks.empty());
}

TEST_F(NewHorizonsWispLongReachAITest, OrdinaryNonReachCreatureDoesNotAcquireDistantMeleeAttacks)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:pikeman"));
	ASSERT_FALSE(battle()->isMeleeAttackPossibleWithLongReach(wisp, target));
	std::shared_ptr<HypotheticBattle> state;
	DamageCache cache;
	EXPECT_TRUE(forecast(state, cache).possibleAttacks.empty());
}

TEST_F(NewHorizonsWispLongReachAITest, GreaterWispAdjacentAttackKeepsItsNoRetaliationTrait)
{
	ASSERT_NO_FATAL_FAILURE(prepare("new-horizons:wispUpgrade", false, BattleHex(5, 4), BattleHex(6, 4)));
	ASSERT_TRUE(battle()->isMeleeAttackPossible(wisp, target));
	std::shared_ptr<HypotheticBattle> state;
	DamageCache cache;
	const auto options = forecast(state, cache);
	ASSERT_FALSE(options.possibleAttacks.empty());
	EXPECT_EQ(options.bestAction().attackerDamageReduce, 0);
	const auto health = wisp->getAvailableHealth();
	const auto beforeAttacks = server.attacks.size();
	runAI();
	ASSERT_EQ(callback->actions.size(), 1u);
	ASSERT_EQ(callback->actions.front().actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->actions.front()));
	EXPECT_EQ(wisp->getAvailableHealth(), health);
	EXPECT_FALSE(std::any_of(server.attacks.begin() + beforeAttacks, server.attacks.end(),
		[](const BattleAttack & attack) { return attack.counter(); }));
}
