/*
 * NewHorizonsBerserkForcedActionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/spells/NewHorizonsMagic.h"

#include "mock/mock_BonusBearer.h"
#include "mock/mock_battle_IBattleState.h"
#include "mock/mock_battle_Unit.h"
#include "mock/mock_scripting_Pool.h"

using namespace battle;
using namespace testing;

namespace
{
class BerserkBattleStateMock : public BattleStateMock
{
public:
	JsonNode magicRules;

	const JsonNode & getMagicRules() const override
	{
		return magicRules;
	}
};

class BerserkUnitFake : public UnitMock
{
public:
	BerserkUnitFake()
		: bonuses(), state(std::make_shared<CUnitStateDetached>(this, this))
	{
	}

	void configure(uint32_t id, BattleSide side, BattleHex position, int speed, bool shooter, bool berserk, const CCreature * type)
	{
		ON_CALL(*this, unitId()).WillByDefault(Return(id));
		ON_CALL(*this, unitSide()).WillByDefault(Return(side));
		ON_CALL(*this, unitOwner()).WillByDefault(Return(PlayerColor(side == BattleSide::ATTACKER ? 0 : 1)));
		ON_CALL(*this, unitType()).WillByDefault(Return(type));
		ON_CALL(*this, getPosition()).WillByDefault(Return(position));
		ON_CALL(*this, doubleWide()).WillByDefault(Return(false));
		ON_CALL(*this, alive()).WillByDefault(Return(true));
		ON_CALL(*this, isGhost()).WillByDefault(Return(false));
		ON_CALL(*this, isValidTarget(_)).WillByDefault(Return(true));
		ON_CALL(*this, canShoot()).WillByDefault(Return(shooter));
		ON_CALL(*this, canShootBlocked()).WillByDefault(Return(false));
		ON_CALL(*this, isShooter()).WillByDefault(Return(shooter));
		ON_CALL(*this, isClone()).WillByDefault(Return(false));
		ON_CALL(*this, isCaster()).WillByDefault(Return(false));
		ON_CALL(*this, unitSlot()).WillByDefault(Return(SlotID(0)));
		ON_CALL(*this, creatureIndex()).WillByDefault(Return(type->getIndex()));
		ON_CALL(*this, creatureId()).WillByDefault(Return(type->getId()));
		ON_CALL(*this, acquireState()).WillByDefault(Return(state));
		ON_CALL(*this, getAllBonuses(_, _)).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getAllBonuses));
		ON_CALL(*this, getTreeVersion()).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getTreeVersion));

		addBonus(BonusType::STACKS_SPEED, speed);
		if(berserk)
			addBonus(BonusType::ATTACKS_NEAREST_CREATURE, 1);
	}

	void addBonus(BonusType type, int value)
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(
			BonusDuration::PERMANENT,
			type,
			BonusSource::CREATURE_ABILITY,
			value,
			CreatureID(0)));
	}

	bool isHypnotized() const override
	{
		return hasBonusOfType(BonusType::HYPNOTIZED);
	}

	bool isInvincible() const override
	{
		return hasBonusOfType(BonusType::INVINCIBLE);
	}

private:
	BonusBearerMock bonuses;
	std::shared_ptr<CUnitState> state;
};

class NewHorizonsBerserkForcedActionTest : public Test
{
public:
	class TestSubject : public CBattleInfoCallback
	{
	public:
		const IBattleInfo * battle = nullptr;

		explicit TestSubject(scripting::Pool * pool)
			: CBattleInfoCallback(), pool(pool)
		{
		}

		const IBattleInfo * getBattle() const override
		{
			return battle;
		}

		std::optional<PlayerColor> getPlayerID() const override
		{
			return std::nullopt;
		}

	private:
		scripting::Pool * pool;
	};

	NiceMock<scripting::PoolMock> pool;
	NiceMock<BerserkBattleStateMock> battle;
	TestSubject callback;
	std::vector<std::unique_ptr<NiceMock<BerserkUnitFake>>> units;
	IBattleInfo::ObstacleCList obstacles;
	const CCreature * creatureType = nullptr;

	NewHorizonsBerserkForcedActionTest()
		: callback(&pool)
	{
	}

	void SetUp() override
	{
		creatureType = CreatureID(CreatureID::IMP).toCreature();
		ASSERT_NE(creatureType, nullptr);
		ASSERT_EQ(creatureType->warMachine, ArtifactID::NONE);

		callback.battle = &battle;
		ON_CALL(battle, getUnitsIf(_)).WillByDefault(Invoke(this, &NewHorizonsBerserkForcedActionTest::getUnitsIf));
		ON_CALL(battle, getBattlefieldType()).WillByDefault(Return(BattleField::NONE));
		ON_CALL(battle, getAllObstacles()).WillByDefault([this]() { return obstacles; });
		ON_CALL(battle, getDefendedTown()).WillByDefault(Return(nullptr));
		ON_CALL(battle, getGateState()).WillByDefault(Return(EGateState::OPENED));
		ON_CALL(battle, getTacticDist()).WillByDefault(Return(0));
		ON_CALL(battle, getSidePlayer(_)).WillByDefault([](BattleSide side)
		{
			return PlayerColor(side == BattleSide::ATTACKER ? 0 : 1);
		});
		ON_CALL(battle, getSideHero(_)).WillByDefault(Return(nullptr));
	}

	void setMagicRules(int version)
	{
		JsonMap rules;
		rules["rulesetVersion"] = JsonNode(version);
		battle.magicRules = JsonNode(rules);
	}

	BerserkUnitFake & addUnit(uint32_t id, BattleSide side, BattleHex position, int speed = 5, bool shooter = false, bool berserk = false)
	{
		auto unit = std::make_unique<NiceMock<BerserkUnitFake>>();
		unit->configure(id, side, position, speed, shooter, berserk, creatureType);
		auto & result = *unit;
		units.push_back(std::move(unit));
		return result;
	}

	void addBlockingObstacle(BattleHex hex)
	{
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->customSize.insert(hex);
		obstacles.push_back(obstacle);
	}

private:
	battle::Units getUnitsIf(const UnitFilter & predicate) const
	{
		battle::Units result;
		for(const auto & unit : units)
			if(predicate(unit.get()))
				result.push_back(unit.get());
		return result;
	}
};
}

TEST_F(NewHorizonsBerserkForcedActionTest, V3ForcesShootersToMakeReachableMeleeAttacks)
{
	setMagicRules(newHorizonsMagic::CURRENT_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 5, true, true);
	auto & target = addUnit(2, BattleSide::DEFENDER, BattleHex(8, 5));

	const auto actions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(actions.size(), 1);
	EXPECT_EQ(actions.front().type, EActionType::WALK_AND_ATTACK);
	EXPECT_EQ(actions.front().target, &target);
	EXPECT_NE(actions.front().type, EActionType::SHOOT);
}

TEST_F(NewHorizonsBerserkForcedActionTest, V3ChoosesNearestTargetsRegardlessOfAllegiance)
{
	setMagicRules(newHorizonsMagic::CURRENT_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 5, false, true);
	auto & friendly = addUnit(2, BattleSide::ATTACKER, BattleHex(6, 5));
	auto & enemy = addUnit(3, BattleSide::DEFENDER, BattleHex(10, 5));

	const auto actions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(actions.size(), 1);
	EXPECT_EQ(actions.front().target, &friendly);
	EXPECT_NE(actions.front().target, &enemy);
}

TEST_F(NewHorizonsBerserkForcedActionTest, V3SkipsTargetsRejectedByAuthoritativeMeleeLegality)
{
	setMagicRules(newHorizonsMagic::CURRENT_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 5, false, true);
	auto & invincibleAlly = addUnit(2, BattleSide::ATTACKER, BattleHex(6, 5));
	invincibleAlly.addBonus(BonusType::INVINCIBLE, 1);
	auto & sanctifiedEnemy = addUnit(3, BattleSide::DEFENDER, BattleHex(7, 5));
	sanctifiedEnemy.addBonus(BonusType::SANCTIFIED, 1);
	auto & legalEnemy = addUnit(4, BattleSide::DEFENDER, BattleHex(10, 5));

	const auto actions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(actions.size(), 1);
	EXPECT_EQ(actions.front().target, &legalEnemy);
}

TEST_F(NewHorizonsBerserkForcedActionTest, V3ReturnsEveryEquallyNearestTargetAndScalarUsesFirst)
{
	setMagicRules(newHorizonsMagic::CURRENT_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(8, 5), 5, false, true);
	auto & firstTarget = addUnit(2, BattleSide::DEFENDER, BattleHex(5, 5));
	auto & secondTarget = addUnit(3, BattleSide::ATTACKER, BattleHex(11, 5));

	const auto actions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(actions.size(), 2);
	EXPECT_EQ(actions[0].target, &firstTarget);
	EXPECT_EQ(actions[1].target, &secondTarget);

	const auto scalarAction = callback.getBerserkForcedAction(&berserker);
	EXPECT_EQ(scalarAction.target, actions.front().target);
}

TEST_F(NewHorizonsBerserkForcedActionTest, V3RanksTargetsByReachableMovementCostInsteadOfRawHexDistance)
{
	setMagicRules(newHorizonsMagic::CURRENT_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 30, false, true);
	auto & rawCloserTarget = addUnit(2, BattleSide::DEFENDER, BattleHex(8, 5));
	auto & pathCloserTarget = addUnit(3, BattleSide::DEFENDER, BattleHex(5, 0));

	// An obstacle wall along q=8 leaves only its two end hexes open, forcing
	// movement to the geometrically closer target around the wall.
	constexpr int wallColumn = 8;
	for(int y = 1; y < GameConstants::BFIELD_HEIGHT - 1; ++y)
		addBlockingObstacle(BattleHex(wallColumn - y / 2, y));

	const auto reachability = callback.getReachability(&berserker);
	const auto nearestReachableAttackCost = [&reachability, &berserker](const BerserkUnitFake & target)
	{
		uint32_t best = ReachabilityInfo::INFINITE_DIST;
		for(const auto & hex : target.getAttackableHexes(&berserker))
			if(reachability.isReachable(hex))
				best = std::min(best, reachability.distances[hex.toInt()]);
		return best;
	};

	const auto rawCloserDistance = BattleHex::getDistance(berserker.getPosition(), rawCloserTarget.getPosition());
	const auto pathCloserDistance = BattleHex::getDistance(berserker.getPosition(), pathCloserTarget.getPosition());
	const auto rawCloserMovementCost = nearestReachableAttackCost(rawCloserTarget);
	const auto pathCloserMovementCost = nearestReachableAttackCost(pathCloserTarget);
	ASSERT_LT(rawCloserDistance, pathCloserDistance);
	ASSERT_LT(pathCloserMovementCost, rawCloserMovementCost);

	const auto actions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(actions.size(), 1);
	EXPECT_EQ(actions.front().target, &pathCloserTarget);
}

TEST_F(NewHorizonsBerserkForcedActionTest, EmptyTargetSetProducesSafeNoAction)
{
	setMagicRules(newHorizonsMagic::CURRENT_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(8, 5), 5, false, true);

	EXPECT_TRUE(callback.getBerserkForcedActions(&berserker).empty());
	const auto action = callback.getBerserkForcedAction(&berserker);
	EXPECT_EQ(action.type, EActionType::NO_ACTION);
	EXPECT_EQ(action.position, berserker.getPosition());
	EXPECT_EQ(action.target, nullptr);
}

TEST_F(NewHorizonsBerserkForcedActionTest, V3ReturnsMovementOnlyActionWhenTargetCannotBeReachedThisActivation)
{
	setMagicRules(newHorizonsMagic::CURRENT_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 1, false, true);
	auto & target = addUnit(2, BattleSide::DEFENDER, BattleHex(10, 5));

	const auto actions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(actions.size(), 1);
	EXPECT_EQ(actions.front().type, EActionType::WALK);
	EXPECT_EQ(actions.front().target, &target);
	EXPECT_TRUE(actions.front().position.isAvailable());
}

TEST_F(NewHorizonsBerserkForcedActionTest, LegacyVersionsAndVanillaRetainRangedShooting)
{
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 5, true, true);
	auto & target = addUnit(2, BattleSide::DEFENDER, BattleHex(9, 5));

	for(const int version : {newHorizonsMagic::RULESET_VERSION, newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION})
	{
		setMagicRules(version);
		const auto actions = callback.getBerserkForcedActions(&berserker);
		ASSERT_EQ(actions.size(), 1);
		EXPECT_EQ(actions.front().type, EActionType::SHOOT);
		EXPECT_EQ(actions.front().target, &target);
	}

	battle.magicRules = JsonNode();
	const auto vanillaActions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(vanillaActions.size(), 1);
	EXPECT_EQ(vanillaActions.front().type, EActionType::SHOOT);
	EXPECT_EQ(vanillaActions.front().target, &target);
}

TEST_F(NewHorizonsBerserkForcedActionTest, LegacyMeleeStillSelectsOnlyTheFirstNearestTarget)
{
	setMagicRules(newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	auto & berserker = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 5, false, true);
	addUnit(2, BattleSide::DEFENDER, BattleHex(13, 5));
	auto & nearestTarget = addUnit(3, BattleSide::ATTACKER, BattleHex(6, 5));

	const auto actions = callback.getBerserkForcedActions(&berserker);
	ASSERT_EQ(actions.size(), 1);
	EXPECT_EQ(actions.front().target, &nearestTarget);
	EXPECT_EQ(callback.getBerserkForcedAction(&berserker).target, &nearestTarget);
}
