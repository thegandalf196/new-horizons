/*
 * NewHorizonsConfusionChoicesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleHexArray.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsConfusion.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameConstants.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_battle_IBattleState.h"
#include "mock/mock_battle_Unit.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/TinyMapGameTest.h"

#include <set>

using namespace battle;
using namespace testing;

namespace
{
class ConfusionUnitFake : public UnitMock
{
	BonusBearerMock bonuses;
	NiceMock<UnitEnvironmentMock> environment;
	std::shared_ptr<CUnitState> state;
public:
	ConfusionUnitFake()
		: state(std::make_shared<CUnitStateDetached>(this, this))
	{}

	void configure(uint32_t id, BattleSide side, BattleHex position, int speed, bool shooter, const CCreature * type)
	{
		ON_CALL(*this, unitId()).WillByDefault(Return(id));
		ON_CALL(*this, unitSide()).WillByDefault(Return(side));
		ON_CALL(*this, unitOwner()).WillByDefault(Return(PlayerColor(side == BattleSide::ATTACKER ? 0 : 1)));
		ON_CALL(*this, unitType()).WillByDefault(Return(type));
		ON_CALL(*this, creatureId()).WillByDefault(Return(type->getId()));
		ON_CALL(*this, creatureIndex()).WillByDefault(Return(type->getIndex()));
		ON_CALL(*this, getPosition()).WillByDefault(Return(position));
		ON_CALL(*this, doubleWide()).WillByDefault(Return(false));
		ON_CALL(*this, alive()).WillByDefault(Return(true));
		ON_CALL(*this, isGhost()).WillByDefault(Return(false));
		ON_CALL(*this, isValidTarget(_)).WillByDefault(Return(true));
		ON_CALL(*this, canMove(_)).WillByDefault(Return(true));
		ON_CALL(*this, canShoot()).WillByDefault(Return(shooter));
		ON_CALL(*this, isShooter()).WillByDefault(Return(shooter));
		ON_CALL(*this, canShootBlocked()).WillByDefault(Return(false));
		ON_CALL(*this, isClone()).WillByDefault(Return(false));
		ON_CALL(*this, isCaster()).WillByDefault(Return(false));
		ON_CALL(*this, getCount()).WillByDefault(Return(1));
		ON_CALL(*this, unitBaseAmount()).WillByDefault(Return(1));
		ON_CALL(*this, getAvailableHealth()).WillByDefault(Return(10));
		ON_CALL(*this, getTotalAttacks(_)).WillByDefault(Return(1));
		ON_CALL(*this, unitSlot()).WillByDefault(Return(SlotID(0)));
		ON_CALL(*this, acquireState()).WillByDefault([this]() { return state->acquireState(); });
		ON_CALL(*this, getAllBonuses(_, _)).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getAllBonuses));
		ON_CALL(*this, getTreeVersion()).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getTreeVersion));
		addBonus(BonusType::STACKS_SPEED, speed);
		addBonus(BonusType::STACK_HEALTH, 10);
		if(shooter)
		{
			addBonus(BonusType::SHOOTER, 1);
			addBonus(BonusType::SHOTS, 12);
		}
		ON_CALL(environment, unitHasAmmoCart(_)).WillByDefault(Return(false));
		ON_CALL(environment, unitEffectiveOwner(_)).WillByDefault([this](const battle::Unit *) { return unitOwner(); });
		state->localInit(&environment);
		state->setPosition(position);
	}

	void addBonus(BonusType type, int value)
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			type, BonusSource::CREATURE_ABILITY, value, CreatureID(0)));
	}

	bool isHypnotized() const override { return hasBonusOfType(BonusType::HYPNOTIZED); }
	bool isInvincible() const override { return hasBonusOfType(BonusType::INVINCIBLE); }
};

class ConfusionCallback : public CBattleInfoCallback
{
public:
	const IBattleInfo * battle = nullptr;
	const IBattleInfo * getBattle() const override { return battle; }
	std::optional<PlayerColor> getPlayerID() const override { return std::nullopt; }
};

class NewHorizonsConfusionChoicesTest : public TinyMapGameTest
{
protected:
	NiceMock<BattleStateMock> battle;
	ConfusionCallback callback;
	std::vector<std::unique_ptr<NiceMock<ConfusionUnitFake>>> units;
	IBattleInfo::ObstacleCList obstacles;
	const CCreature * creatureType = nullptr;

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		creatureType = CreatureID(CreatureID::IMP).toCreature();
		ASSERT_NE(creatureType, nullptr);
		callback.battle = &battle;
		ON_CALL(battle, getUnitsIf(_)).WillByDefault([this](const UnitFilter & predicate)
		{
			battle::Units result;
			for(const auto & unit : units)
				if(predicate(unit.get()))
					result.push_back(unit.get());
			return result;
		});
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

	ConfusionUnitFake & addUnit(uint32_t id, BattleSide side, BattleHex position, int speed = 5, bool shooter = false)
	{
		auto unit = std::make_unique<NiceMock<ConfusionUnitFake>>();
		unit->configure(id, side, position, speed, shooter, creatureType);
		auto & result = *unit;
		units.push_back(std::move(unit));
		return result;
	}

	void block(BattleHex hex)
	{
		auto obstacle = std::make_shared<SpellCreatedObstacle>();
		obstacle->customSize.insert(hex);
		obstacles.push_back(obstacle);
	}

	const newHorizonsConfusion::AttackTargetChoices * group(const newHorizonsConfusion::Choices & choices, uint32_t id)
	{
		for(const auto & candidate : choices.attacks)
			if(candidate.targetId == id)
				return &candidate;
		return nullptr;
	}

	std::set<BattleHex> meleePositions(const newHorizonsConfusion::AttackTargetChoices & target)
	{
		std::set<BattleHex> result;
		for(const auto & action : target.attacks)
			if(action.type == EActionType::WALK_AND_ATTACK)
				result.insert(action.position);
		return result;
	}

	bool containsShoot(const newHorizonsConfusion::AttackTargetChoices & target)
	{
		return std::ranges::any_of(target.attacks, [](const ForcedAction & action)
		{
			return action.type == EActionType::SHOOT;
		});
	}
};
}

TEST_F(NewHorizonsConfusionChoicesTest, EnemyGroupsRemainDistinctDespiteUnequalAttackPositionCounts)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 10);
	addUnit(2, BattleSide::ATTACKER, BattleHex(3, 5));
	auto & constrained = addUnit(3, BattleSide::DEFENDER, BattleHex(6, 5));
	auto & open = addUnit(4, BattleSide::DEFENDER, BattleHex(6, 2));
	const BattleHex onlyPosition(5, 5);
	for(const auto & hex : BattleHexArray::getNeighbouringTiles(constrained.getPosition()))
		if(hex != onlyPosition)
			block(hex);
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_EQ(choices.attacks.size(), 2u);
	EXPECT_EQ(group(choices, 2), nullptr);
	const auto * narrowGroup = group(choices, constrained.unitId());
	const auto * broadGroup = group(choices, open.unitId());
	ASSERT_NE(narrowGroup, nullptr);
	ASSERT_NE(broadGroup, nullptr);
	EXPECT_EQ(meleePositions(*narrowGroup), std::set<BattleHex>{onlyPosition});
	EXPECT_GT(meleePositions(*broadGroup).size(), narrowGroup->attacks.size());
}

TEST_F(NewHorizonsConfusionChoicesTest, OpenMeleeReturnsEveryLegalPositionNotOnlyNearest)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 10);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(8, 5));
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	const auto * target = group(choices, enemy.unitId());
	ASSERT_NE(target, nullptr);
	std::set<BattleHex> expected;
	for(const auto & hex : BattleHexArray::getNeighbouringTiles(enemy.getPosition()))
		expected.insert(hex);
	ASSERT_EQ(expected.size(), 6u);
	EXPECT_EQ(meleePositions(*target), expected);
	EXPECT_EQ(target->attacks.size(), expected.size());
	EXPECT_TRUE(target->furthestAdvances.empty());
	EXPECT_FALSE(target->zeroAdvanceDefends);
}

TEST_F(NewHorizonsConfusionChoicesTest, OrdinaryShooterRetainsBothShotAndLegalMeleeAlternatives)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 10, true);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(8, 5));
	ASSERT_TRUE(callback.battleCanShootAction(&actor, enemy.getPosition()));
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	const auto * target = group(choices, enemy.unitId());
	ASSERT_NE(target, nullptr);
	EXPECT_TRUE(containsShoot(*target));
	EXPECT_EQ(meleePositions(*target).size(), 6u);
	for(const auto & action : target->attacks)
		EXPECT_FALSE(action.skirmisher) << "ordinary shot/melee is not a moved Skirmisher shot";
}

TEST_F(NewHorizonsConfusionChoicesTest, ExhaustedAndEnemyBlockedShootingUseOrdinaryRestrictions)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 10, true);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(8, 5));
	ON_CALL(actor, canShoot()).WillByDefault(Return(false));
	const auto exhausted = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(exhausted, enemy.unitId()), nullptr);
	EXPECT_FALSE(containsShoot(*group(exhausted, enemy.unitId())));
	ON_CALL(actor, canShoot()).WillByDefault(Return(true));
	addUnit(3, BattleSide::DEFENDER, BattleHex(5, 5));
	ASSERT_FALSE(callback.battleCanShootAction(&actor, enemy.getPosition()));
	const auto blocked = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(blocked, enemy.unitId()), nullptr);
	EXPECT_FALSE(containsShoot(*group(blocked, enemy.unitId())));
}

TEST_F(NewHorizonsConfusionChoicesTest, UnreachableEnemyUsesSharedFurthestLegalAdvance)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 2);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(12, 5));
	block(BattleHex(5, 5));
	const auto reachability = callback.getReachability(&actor);
	uint32_t nearestCost = ReachabilityInfo::INFINITE_DIST;
	for(const auto & anchor : enemy.getAttackableHexes(&actor))
		if(reachability.isReachable(anchor))
			nearestCost = std::min(nearestCost, reachability.distances[anchor.toInt()]);
	std::set<BattleHex> expected;
	for(const auto & anchor : enemy.getAttackableHexes(&actor))
		if(reachability.isReachable(anchor) && reachability.distances[anchor.toInt()] == nearestCost)
			expected.insert(callback.getClosestHexToTargetInRange(reachability, actor, anchor));
	ASSERT_FALSE(expected.empty());
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	const auto * target = group(choices, enemy.unitId());
	ASSERT_NE(target, nullptr);
	EXPECT_TRUE(target->attacks.empty());
	ASSERT_EQ(target->furthestAdvances.size(), 1u);
	const auto advance = target->furthestAdvances.front();
	EXPECT_TRUE(expected.contains(advance));
	EXPECT_NE(advance, actor.getPosition());
	EXPECT_TRUE(reachability.isReachable(advance));
	EXPECT_LE(reachability.distances[advance.toInt()], actor.getMovementRange());
	EXPECT_FALSE(target->zeroAdvanceDefends);
}

TEST_F(NewHorizonsConfusionChoicesTest, WanderListsAllAndOnlyOrdinaryNonstationaryMovementEndpoints)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(8, 5), 2);
	auto & ally = addUnit(2, BattleSide::ATTACKER, BattleHex(9, 5));
	block(BattleHex(7, 5));
	const auto legal = callback.battleGetAvailableHexes(&actor, false);
	std::set<BattleHex> expected;
	for(const auto & hex : legal)
		if(hex != actor.getPosition())
			expected.insert(hex);
	ASSERT_FALSE(expected.empty());
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	EXPECT_EQ(std::set<BattleHex>(choices.wanderDestinations.begin(), choices.wanderDestinations.end()), expected);
	EXPECT_EQ(choices.wanderDestinations.size(), expected.size());
	EXPECT_FALSE(expected.contains(ally.getPosition()));
	EXPECT_FALSE(expected.contains(BattleHex(7, 5)));
	EXPECT_FALSE(choices.wanderFallsBackToDefend);
	EXPECT_TRUE(choices.attacks.empty());
}

TEST_F(NewHorizonsConfusionChoicesTest, TrappedWanderAndZeroAdvanceAttackResolveAsDefend)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 3);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(12, 5));
	for(const auto & hex : BattleHexArray::getNeighbouringTiles(actor.getPosition()))
		block(hex);
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	EXPECT_TRUE(choices.wanderDestinations.empty());
	EXPECT_TRUE(choices.wanderFallsBackToDefend);
	const auto * target = group(choices, enemy.unitId());
	ASSERT_NE(target, nullptr);
	EXPECT_TRUE(target->attacks.empty());
	EXPECT_TRUE(target->furthestAdvances.empty());
	EXPECT_TRUE(target->zeroAdvanceDefends);
}

TEST_F(NewHorizonsConfusionChoicesTest, DoubleWideWanderRejectsBlockedSecondFootprint)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(8, 5), 3);
	ON_CALL(actor, doubleWide()).WillByDefault(Return(true));
	const BattleHex candidate(10, 5);
	const BattleHex blockedSecond(9, 5);
	block(blockedSecond);
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	EXPECT_FALSE(vstd::contains(choices.wanderDestinations, candidate));
	EXPECT_FALSE(vstd::contains(choices.wanderDestinations, blockedSecond));
	for(const auto & hex : choices.wanderDestinations)
	{
		EXPECT_TRUE(callback.getReachability(&actor).isReachable(hex));
		EXPECT_FALSE(actor.getHexes(hex).contains(blockedSecond));
	}
}

TEST_F(NewHorizonsConfusionChoicesTest, FlyingTraversesObstacleRingButStillCannotLandOnIt)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(8, 5), 3);
	for(const auto & hex : BattleHexArray::getNeighbouringTiles(actor.getPosition()))
		block(hex);
	EXPECT_TRUE(newHorizonsConfusion::enumerateChoices(callback, &actor).wanderDestinations.empty());
	actor.addBonus(BonusType::FLYING, 1);
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	EXPECT_FALSE(choices.wanderDestinations.empty());
	for(const auto & blocked : BattleHexArray::getNeighbouringTiles(actor.getPosition()))
		EXPECT_FALSE(vstd::contains(choices.wanderDestinations, blocked));
	EXPECT_FALSE(choices.wanderFallsBackToDefend);
}

TEST_F(NewHorizonsConfusionChoicesTest, InvalidActorProducesNoActionsWithoutMutatingBattle)
{
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, nullptr);
	EXPECT_TRUE(choices.attacks.empty());
	EXPECT_TRUE(choices.wanderDestinations.empty());
	EXPECT_TRUE(units.empty());
	EXPECT_TRUE(obstacles.empty());
}

TEST_F(NewHorizonsConfusionChoicesTest, OrdinaryTargetLegalityExcludesInvalidAndProtectedEnemies)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 10, true);
	auto & invalid = addUnit(2, BattleSide::DEFENDER, BattleHex(8, 2));
	ON_CALL(invalid, isValidTarget(_)).WillByDefault(Return(false));
	auto & invincible = addUnit(3, BattleSide::DEFENDER, BattleHex(8, 4));
	invincible.addBonus(BonusType::INVINCIBLE, 1);
	auto & protectedEnemy = addUnit(4, BattleSide::DEFENDER, BattleHex(8, 6));
	protectedEnemy.addBonus(BonusType::SANCTIFIED, 1);
	auto & ordinary = addUnit(5, BattleSide::DEFENDER, BattleHex(8, 8));
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	EXPECT_EQ(group(choices, invalid.unitId()), nullptr);
	EXPECT_EQ(group(choices, invincible.unitId()), nullptr);
	EXPECT_EQ(group(choices, protectedEnemy.unitId()), nullptr);
	EXPECT_NE(group(choices, ordinary.unitId()), nullptr);
}

TEST_F(NewHorizonsConfusionChoicesTest, ChangedActionControllerDefinesEnemiesNotPhysicalSide)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 10);
	actor.addBonus(BonusType::HYPNOTIZED, 1);
	auto & formerAlly = addUnit(2, BattleSide::ATTACKER, BattleHex(8, 3));
	auto & controllerAlly = addUnit(3, BattleSide::DEFENDER, BattleHex(8, 7));
	ASSERT_TRUE(callback.battleMatchActionController(&actor, &formerAlly, false));
	ASSERT_TRUE(callback.battleMatchActionController(&actor, &controllerAlly, true));
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	EXPECT_NE(group(choices, formerAlly.unitId()), nullptr);
	EXPECT_EQ(group(choices, controllerAlly.unitId()), nullptr);
	EXPECT_EQ(actor.unitSide(), BattleSide::ATTACKER);
}

TEST_F(NewHorizonsConfusionChoicesTest, StoppingHazardRingAllowsEntryButNotMovementBeyond)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(8, 5), 5);
	for(const auto & hex : BattleHexArray::getNeighbouringTiles(actor.getPosition()))
	{
		auto hazard = std::make_shared<SpellCreatedObstacle>();
		hazard->customSize.insert(hex);
		hazard->passable = true;
		hazard->trap = true;
		hazard->hidden = false;
		hazard->casterSide = BattleSide::DEFENDER;
		hazard->turnsRemaining = 5;
		obstacles.push_back(hazard);
	}
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(10, 5));
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	std::set<BattleHex> expected;
	for(const auto & hex : BattleHexArray::getNeighbouringTiles(actor.getPosition()))
		expected.insert(hex);
	EXPECT_EQ(std::set<BattleHex>(choices.wanderDestinations.begin(), choices.wanderDestinations.end()), expected);
	EXPECT_FALSE(choices.wanderFallsBackToDefend);
	ASSERT_NE(group(choices, enemy.unitId()), nullptr);
	EXPECT_TRUE(group(choices, enemy.unitId())->attacks.empty())
		<< "entering a stopping hazard ends ground movement before its planned melee attack";
	actor.addBonus(BonusType::FLYING, 1);
	const auto flying = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(flying, enemy.unitId()), nullptr);
	EXPECT_FALSE(group(flying, enemy.unitId())->attacks.empty());
}

TEST_F(NewHorizonsConfusionChoicesTest, LongWeaponUsesProjectedAttackPositionAndClearMiddleHex)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 6);
	actor.addBonus(BonusType::LONG_WEAPON, 1);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(8, 5));
	const BattleHex longWeaponPosition(6, 5);
	ASSERT_FALSE(callback.isLongWeaponAttack(&actor, &enemy));
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(choices, enemy.unitId()), nullptr);
	EXPECT_TRUE(meleePositions(*group(choices, enemy.unitId())).contains(longWeaponPosition));
	EXPECT_FALSE(meleePositions(*group(choices, enemy.unitId())).contains(actor.getPosition()));
	EXPECT_EQ(actor.getPosition(), BattleHex(4, 5)) << "enumeration must not move the live unit";
	block(BattleHex(7, 5));
	const auto blocked = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(blocked, enemy.unitId()), nullptr);
	EXPECT_FALSE(meleePositions(*group(blocked, enemy.unitId())).contains(longWeaponPosition));
	EXPECT_EQ(actor.getPosition(), BattleHex(4, 5));
}

TEST_F(NewHorizonsConfusionChoicesTest, SkirmisherMovedShotsCarryExplicitMetadataAndExactLegalDestinations)
{
	if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
		GTEST_SKIP() << "Requires the New Horizons module for actual Skirmisher acquisition";
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{CreatureID(0), 1}});
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto archery = SecondarySkill(SecondarySkill::decode("new-horizons:archery"));
	hero->setSecSkillLevel(archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	hero->applyPerkSelection({"new-horizons:archery", "new-horizons:archery.skirmisher"});
	ASSERT_TRUE(newHorizonsArchery::hasSkirmisher(hero));
	ON_CALL(battle, getSideHero(BattleSide::ATTACKER)).WillByDefault(Return(hero));
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 5), 6, true);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(12, 5));
	const auto projected = actor.acquireState();
	ASSERT_TRUE(projected->alive());
	ASSERT_TRUE(projected->canShoot());
	ASSERT_EQ(projected->getAvailableHealth(), actor.getAvailableHealth());
	ASSERT_EQ(projected->getPosition(), actor.getPosition());
	EXPECT_NE(projected, actor.acquireState()) << "each projection must have independent mutable state";
	const auto legal = callback.battleGetSkirmisherAttackFromHexes(&actor, enemy.getPosition());
	ASSERT_FALSE(legal.empty());
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	const auto * target = group(choices, enemy.unitId());
	ASSERT_NE(target, nullptr);
	std::set<BattleHex> movedShots;
	for(const auto & action : target->attacks)
		if(action.skirmisher)
		{
			EXPECT_EQ(action.type, EActionType::SHOOT);
			EXPECT_NE(action.position, actor.getPosition());
			EXPECT_EQ(action.target, &enemy);
			movedShots.insert(action.position);
		}
	EXPECT_EQ(movedShots, std::set<BattleHex>(legal.begin(), legal.end()));
	EXPECT_TRUE(std::ranges::any_of(target->attacks, [&actor](const auto & action)
	{
		return action.type == EActionType::SHOOT && action.position == actor.getPosition() && !action.skirmisher;
	}));
	const auto stoppingHex = *legal.begin();
	auto hazard = std::make_shared<SpellCreatedObstacle>();
	hazard->customSize.insert(stoppingHex);
	hazard->passable = true;
	hazard->trap = true;
	hazard->hidden = false;
	hazard->casterSide = BattleSide::DEFENDER;
	hazard->turnsRemaining = 5;
	obstacles.push_back(hazard);
	const auto movedShotEndsThere = [stoppingHex](const auto & action)
	{
		return action.skirmisher && action.type == EActionType::SHOOT && action.position == stoppingHex;
	};
	const auto stopped = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(stopped, enemy.unitId()), nullptr);
	EXPECT_FALSE(std::ranges::any_of(group(stopped, enemy.unitId())->attacks, movedShotEndsThere))
		<< "a ground shooter entering a stopping hazard cannot fire afterward";
	actor.addBonus(BonusType::FLYING, 1);
	const auto flying = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(flying, enemy.unitId()), nullptr);
	EXPECT_TRUE(std::ranges::any_of(group(flying, enemy.unitId())->attacks, movedShotEndsThere));
}

TEST_F(NewHorizonsConfusionChoicesTest, LongWeaponVacatesOriginalMiddleButRetainsUnrelatedMiddleBlocker)
{
	auto & actor = addUnit(1, BattleSide::ATTACKER, BattleHex(4, 4), 6);
	actor.addBonus(BonusType::LONG_WEAPON, 1);
	auto & enemy = addUnit(2, BattleSide::DEFENDER, BattleHex(5, 4));
	const BattleHex vacatingAttackFrom(3, 4);
	ASSERT_TRUE(callback.isLongWeaponAttack(&actor, &enemy, vacatingAttackFrom));
	const auto choices = newHorizonsConfusion::enumerateChoices(callback, &actor);
	ASSERT_NE(group(choices, enemy.unitId()), nullptr);
	EXPECT_TRUE(meleePositions(*group(choices, enemy.unitId())).contains(vacatingAttackFrom));
	EXPECT_EQ(actor.getPosition(), BattleHex(4, 4));

	auto & otherActor = addUnit(3, BattleSide::ATTACKER, BattleHex(8, 8), 6);
	otherActor.addBonus(BonusType::LONG_WEAPON, 1);
	auto & otherEnemy = addUnit(4, BattleSide::DEFENDER, BattleHex(12, 8));
	addUnit(5, BattleSide::ATTACKER, BattleHex(11, 8));
	const BattleHex blockedAttackFrom(10, 8);
	ASSERT_FALSE(callback.isLongWeaponAttack(&otherActor, &otherEnemy, blockedAttackFrom));
	const auto blocked = newHorizonsConfusion::enumerateChoices(callback, &otherActor);
	ASSERT_NE(group(blocked, otherEnemy.unitId()), nullptr);
	EXPECT_FALSE(meleePositions(*group(blocked, otherEnemy.unitId())).contains(blockedAttackFrom));
	EXPECT_EQ(otherActor.getPosition(), BattleHex(8, 8));
}
