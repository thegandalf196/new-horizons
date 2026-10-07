/*
 * NewHorizonsPassThroughLongReachTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/CStack.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/BattleHex.h"
#include "../../../lib/battle/ReachabilityInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <ranges>

namespace
{
class NewHorizonsPassThroughLongReachTest : public BattleTestFixture
{
protected:
	void removeStartingUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void grant(CStack * stack, BonusType type, int32_t value = 0)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::CREATURE_ABILITY, value, BonusSourceID(stack->creatureId())));
	}

	bool act(const CStack * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	void startControlledBattle()
	{
		startGame();
		startBattle();
		removeStartingUnits();
	}
};
}

TEST_F(NewHorizonsPassThroughLongReachTest, PassThroughTraversesStacksAndSolidObstaclesButKeepsLegalEndpointsAndBudget)
{
	startControlledBattle();
	const BattleHex start(8, 2);
	const BattleHex obstacleHex(8, 3);
	const BattleHex blockerHex(7, 4);
	const BattleHex attackPosition(7, 5);
	const BattleHex targetHex(8, 5);
	auto * mover = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), start, 10);
	auto * blocker = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), blockerHex, 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), targetHex, 1000);
	ASSERT_NE(mover, nullptr);
	ASSERT_NE(blocker, nullptr);
	ASSERT_NE(target, nullptr);
	grant(mover, BonusType::PASS_THROUGH);

	SpellCreatedObstacle forceField;
	forceField.uniqueID = 318;
	forceField.ID = SpellID::FORCE_FIELD;
	forceField.trigger = SpellID::NONE;
	forceField.pos = obstacleHex;
	forceField.casterSide = BattleSide::NONE;
	forceField.turnsRemaining = 3;
	forceField.passable = false;
	forceField.trap = false;
	forceField.hidden = false;
	forceField.nativeVisible = false;
	forceField.customSize.insert(obstacleHex);
	BattleObstaclesChanged addObstacle;
	addObstacle.battleID = BattleID(0);
	addObstacle.change = ObstacleChanges(forceField.uniqueID, BattleChanges::EOperation::ADD);
	forceField.toInfo(addObstacle.change);
	gameHandler->sendAndApply(addObstacle);

	beginCombat();
	const auto reachability = battle()->getReachability(mover);
	ASSERT_TRUE(reachability.params.passThrough);
	ASSERT_TRUE(reachability.isReachable(attackPosition));
	EXPECT_FALSE(reachability.isReachable(obstacleHex))
		<< "Transit ability does not make the solid obstacle a legal endpoint";
	EXPECT_FALSE(reachability.isReachable(blockerHex))
		<< "Transit ability does not make an occupied stack a legal endpoint";
	EXPECT_LT(reachability.distances[attackPosition.toInt()], ReachabilityInfo::INFINITE_DIST);
	EXPECT_LT(reachability.distances[obstacleHex.toInt()], ReachabilityInfo::INFINITE_DIST);
	EXPECT_LT(reachability.distances[blockerHex.toInt()], ReachabilityInfo::INFINITE_DIST);
	EXPECT_TRUE(std::ranges::any_of(battle()->getPath(start, attackPosition, mover).first,
		[&](const BattleHex & hex) { return hex == blockerHex; }));

	BattleHex overBudget = BattleHex::INVALID;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex candidate(static_cast<si16>(index));
		if(reachability.isReachable(candidate)
			&& reachability.distances[candidate.toInt()] > static_cast<uint32_t>(mover->getMovementRange(0)))
		{
			overBudget = candidate;
			break;
		}
	}
	ASSERT_TRUE(overBudget.isValid()) << "The field has a legal endpoint outside this stack's movement budget";
	activate(mover);
	EXPECT_FALSE(act(mover, BattleAction::makeMove(mover, overBudget)));
	EXPECT_EQ(mover->getPosition(), start) << "PASS_THROUGH does not add movement points";

	activate(mover);
	EXPECT_FALSE(act(mover, BattleAction::makeMove(mover, blockerHex)));
	EXPECT_EQ(mover->getPosition(), start) << "An occupied transit hex remains illegal as the requested endpoint";

	activate(mover);
	const auto attackStart = server.attacks.size();
	const auto action = BattleAction::makeMeleeAttack(mover, targetHex, attackPosition, false);
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_TRUE(act(mover, action)) << "WALK_AND_ATTACK replays through both transit-only cells";
	EXPECT_EQ(mover->getPosition(), attackPosition);
	EXPECT_EQ(blocker->getPosition(), blockerHex);
	const auto ordinaryRetaliation = std::ranges::find_if(
		server.attacks.begin() + static_cast<std::ptrdiff_t>(attackStart), server.attacks.end(),
		[&](const BattleAttack & attack)
		{
			return attack.counter() && attack.stackAttacking == target->unitId();
		});
	EXPECT_NE(ordinaryRetaliation, server.attacks.end())
		<< "A normal adjacent WALK_AND_ATTACK keeps ordinary retaliation";
}

TEST_F(NewHorizonsPassThroughLongReachTest, PassThroughStillTriggersAndStopsAtMovementStoppingHazards)
{
	startControlledBattle();
	const BattleHex start(8, 2);
	const BattleHex trapHex(8, 3);
	const BattleHex beyondTrap(8, 4);
	auto * mover = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), start, 10);
	ASSERT_NE(mover, nullptr);
	grant(mover, BonusType::PASS_THROUGH);

	SpellCreatedObstacle trap;
	trap.uniqueID = 319;
	trap.ID = SpellID::QUICKSAND;
	trap.trigger = SpellID::NONE;
	trap.pos = BattleHex(1, trapHex.getY());
	trap.casterSide = BattleSide::NONE;
	trap.turnsRemaining = 3;
	trap.passable = true;
	trap.trap = true;
	trap.hidden = false;
	trap.nativeVisible = false;
	// A single trapped hex can be bypassed by routing through an adjacent
	// column. Cover the full playable row so this fixture tests hazard stopping,
	// not whether the pathfinder can choose a normal detour around a trap.
	for(int x = 1; x < GameConstants::BFIELD_WIDTH - 1; ++x)
		trap.customSize.insert(BattleHex(x, trapHex.getY()));
	BattleObstaclesChanged addTrap;
	addTrap.battleID = BattleID(0);
	addTrap.change = ObstacleChanges(trap.uniqueID, BattleChanges::EOperation::ADD);
	trap.toInfo(addTrap.change);
	gameHandler->sendAndApply(addTrap);

	beginCombat();
	const auto reachability = battle()->getReachability(mover);
	EXPECT_TRUE(reachability.isReachable(trapHex));
	EXPECT_FALSE(reachability.isReachable(beyondTrap))
		<< "A movement-stopping trap still stops the ordinary movement search";

	activate(mover);
	EXPECT_FALSE(act(mover, BattleAction::makeMove(mover, beyondTrap)))
		<< "A destination past a movement-stopping trap is rejected as unreachable";
	EXPECT_EQ(mover->getPosition(), start);

	activate(mover);
	EXPECT_TRUE(act(mover, BattleAction::makeMove(mover, trapHex)));
	EXPECT_EQ(mover->getPosition(), trapHex)
		<< "PASS_THROUGH resolves the trap at its legal endpoint and does not bypass its stop";
}

TEST_F(NewHorizonsPassThroughLongReachTest, LongReachWalkAndAttackUsesNoRetaliationButKeepsIncomingAndOrdinaryAdjacentCounters)
{
	startControlledBattle();
	const auto pikeman = creatureByName("core:pikeman");
	const auto peasant = creatureByName("core:peasant");
	auto * rangedAttacker = addStack(BattleSide::ATTACKER, pikeman, BattleHex(5, 2), 10);
	auto * rangedTarget = addStack(BattleSide::DEFENDER, peasant, BattleHex(12, 2), 1000);
	auto * adjacentAttacker = addStack(BattleSide::ATTACKER, pikeman, BattleHex(5, 7), 10);
	auto * adjacentTarget = addStack(BattleSide::DEFENDER, peasant, BattleHex(6, 7), 1000);
	auto * ordinaryAttacker = addStack(BattleSide::ATTACKER, pikeman, BattleHex(2, 7), 10);
	auto * ordinaryTarget = addStack(BattleSide::DEFENDER, peasant, BattleHex(3, 7), 1000);
	auto * incomingAttacker = addStack(BattleSide::ATTACKER, pikeman, BattleHex(10, 9), 10);
	auto * longReachDefender = addStack(BattleSide::DEFENDER, peasant, BattleHex(11, 9), 1000);
	ASSERT_NE(rangedAttacker, nullptr);
	ASSERT_NE(rangedTarget, nullptr);
	ASSERT_NE(adjacentAttacker, nullptr);
	ASSERT_NE(adjacentTarget, nullptr);
	ASSERT_NE(ordinaryAttacker, nullptr);
	ASSERT_NE(ordinaryTarget, nullptr);
	ASSERT_NE(incomingAttacker, nullptr);
	ASSERT_NE(longReachDefender, nullptr);
	grant(rangedAttacker, BonusType::LONG_REACH, 5);
	grant(rangedAttacker, BonusType::BLOCKS_RETALIATION);
	grant(adjacentAttacker, BonusType::LONG_REACH, 5);
	grant(adjacentAttacker, BonusType::BLOCKS_RETALIATION);
	grant(longReachDefender, BonusType::LONG_REACH, 5);
	grant(longReachDefender, BonusType::BLOCKS_RETALIATION);
	beginCombat();

	const BattleHex rangedAttackPosition(6, 2);
	EXPECT_TRUE(battle()->isMeleeAttackPossibleWithLongReach(rangedAttacker, rangedTarget,
		rangedAttackPosition, rangedTarget->getPosition()));
	EXPECT_FALSE(battle()->isMeleeAttackPossible(rangedAttacker, rangedTarget,
		rangedAttackPosition, rangedTarget->getPosition()));
	activate(rangedAttacker);
	const auto distantAttackStart = server.attacks.size();
	const auto distantAction = BattleAction::makeMeleeAttack(
		rangedAttacker, rangedTarget->getPosition(), rangedAttackPosition, false);
	ASSERT_EQ(distantAction.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_TRUE(act(rangedAttacker, distantAction))
		<< "The authoritative WALK_AND_ATTACK accepts Long Reach 5 at maximum distance 6";
	EXPECT_EQ(rangedAttacker->getPosition(), rangedAttackPosition);
	EXPECT_EQ(std::ranges::count_if(server.attacks.begin() + static_cast<std::ptrdiff_t>(distantAttackStart),
		server.attacks.end(), [&](const BattleAttack & attack)
		{
			return attack.counter() && attack.stackAttacking == rangedTarget->unitId();
		}), 0);

	activate(adjacentAttacker);
	const auto adjacentAttackStart = server.attacks.size();
	ASSERT_TRUE(act(adjacentAttacker, BattleAction::makeMeleeAttack(
		adjacentAttacker, adjacentTarget->getPosition(), adjacentAttacker->getPosition(), false)));
	EXPECT_EQ(std::ranges::count_if(server.attacks.begin() + static_cast<std::ptrdiff_t>(adjacentAttackStart),
		server.attacks.end(), [&](const BattleAttack & attack)
		{
			return attack.counter() && attack.stackAttacking == adjacentTarget->unitId();
		}), 0)
		<< "The approved creature BLOCKS_RETALIATION ability suppresses counters even at adjacency";

	activate(ordinaryAttacker);
	const auto ordinaryAttackStart = server.attacks.size();
	ASSERT_TRUE(act(ordinaryAttacker, BattleAction::makeMeleeAttack(
		ordinaryAttacker, ordinaryTarget->getPosition(), ordinaryAttacker->getPosition(), false)));
	EXPECT_NE(std::ranges::find_if(server.attacks.begin() + static_cast<std::ptrdiff_t>(ordinaryAttackStart),
		server.attacks.end(), [&](const BattleAttack & attack)
		{
			return attack.counter() && attack.stackAttacking == ordinaryTarget->unitId();
		}), server.attacks.end())
		<< "An ordinary adjacent attack keeps its normal retaliation";

	activate(incomingAttacker);
	const auto incomingAttackStart = server.attacks.size();
	ASSERT_TRUE(act(incomingAttacker, BattleAction::makeMeleeAttack(
		incomingAttacker, longReachDefender->getPosition(), incomingAttacker->getPosition(), false)));
	EXPECT_NE(std::ranges::find_if(server.attacks.begin() + static_cast<std::ptrdiff_t>(incomingAttackStart),
		server.attacks.end(), [&](const BattleAttack & attack)
		{
			return attack.counter() && attack.stackAttacking == longReachDefender->unitId();
		}), server.attacks.end())
		<< "A Long Reach stack still makes its ordinary adjacent retaliation when attacked";
}

TEST_F(NewHorizonsPassThroughLongReachTest, LongReachDoesNotAttackThroughAnOccupiedCorridor)
{
	startControlledBattle();
	const auto pikeman = creatureByName("core:pikeman");
	const auto peasant = creatureByName("core:peasant");
	auto * attacker = addStack(BattleSide::ATTACKER, pikeman, BattleHex(5, 4), 10);
	auto * screen = addStack(BattleSide::DEFENDER, peasant, BattleHex(8, 4), 10);
	auto * target = addStack(BattleSide::DEFENDER, peasant, BattleHex(11, 4), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(screen, nullptr);
	ASSERT_NE(target, nullptr);
	grant(attacker, BonusType::LONG_REACH, 5);
	beginCombat();

	EXPECT_FALSE(battle()->isMeleeAttackPossible(attacker, target));
	EXPECT_FALSE(battle()->isMeleeAttackPossibleWithLongReach(attacker, target));
	activate(attacker);
	const auto targetCount = target->getCount();
	EXPECT_FALSE(act(attacker, BattleAction::makeMeleeAttack(
		attacker, target->getPosition(), attacker->getPosition(), false)))
		<< "The authoritative WALK_AND_ATTACK action rejects a blocked Long Reach corridor";
	EXPECT_EQ(attacker->getPosition(), BattleHex(5, 4));
	EXPECT_EQ(target->getCount(), targetCount);
}
