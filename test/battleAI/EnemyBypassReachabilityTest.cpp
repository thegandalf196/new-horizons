/*
 * EnemyBypassReachabilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/EnemyBypassReachability.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/bonuses/Bonus.h"

namespace
{
using Units = std::map<uint32_t, std::pair<JsonNode, std::vector<JsonNode>>>;

class StopperProbe final : public CPlayerBattleCallback
{
	const std::optional<PlayerColor> observer;
public:
	explicit StopperProbe(const CBattleInfoCallback & model)
		: CPlayerBattleCallback(model.getBattle(), model.getPlayerID().value_or(PlayerColor::CANNOT_DETERMINE)),
		  observer(model.getPlayerID())
	{}
	std::optional<PlayerColor> getPlayerID() const override { return observer; }
	using CBattleInfoCallback::getStoppers;
};

class EnemyBypassReachabilityTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<HypotheticBattle> parent;
	std::shared_ptr<HypotheticBattle> child;
	uint32_t leftId = 0;
	uint32_t rightId = 0;
	BattleSide observer = BattleSide::ATTACKER;

	void addColumn(int id, int x, BattleSide caster, bool hidden, bool trap, int cost)
	{
		SpellCreatedObstacle obstacle;
		obstacle.uniqueID = id;
		obstacle.ID = SpellID::QUICKSAND;
		obstacle.pos = BattleHex(x, 5);
		obstacle.casterSide = caster;
		obstacle.hidden = hidden;
		obstacle.nativeVisible = false;
		obstacle.passable = true;
		obstacle.trap = trap;
		obstacle.movementCost = cost;
		for(int y = 0; y < GameConstants::BFIELD_HEIGHT; ++y)
			obstacle.customSize.insert(BattleHex(x, y));
		BattleObstaclesChanged pack;
		pack.battleID = BattleID(0);
		obstacle.toInfo(pack.change);
		gameHandler->sendAndApply(pack);
	}

	void capture(BattleSide side)
	{
		observer = side;
		environment = std::make_shared<projectedHitTest::Environment>(gameState());
		std::shared_ptr<CBattleInfoCallback> callback;
		if(side == BattleSide::ALL_KNOWING)
			callback = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		else
			callback = std::make_shared<CPlayerBattleCallback>(battle(),
				side == BattleSide::ATTACKER ? attackerSideHero->getOwner() : defenderSideHero->getOwner());
		parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		ASSERT_EQ(child->battleGetMySide(), side);
		ASSERT_EQ(battle()->getAllObstacles().size(), 3u);
		ASSERT_EQ(parent->getAllObstacles().size(), side == BattleSide::ALL_KNOWING ? 3u : 2u);
		for(const auto & obstacle : parent->getAllObstacles())
			if(side != BattleSide::ALL_KNOWING)
				ASSERT_NE(obstacle->uniqueID, 3);
	}

	void prepare(BattleSide side, bool flying = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		const auto creature = creatureByName(flying ? "core:angel" : "core:pikeman");
		leftId = addStack(BattleSide::ATTACKER, creature, BattleHex(6, 4), 10)->unitId();
		rightId = addStack(BattleSide::DEFENDER, creature, BattleHex(6, 7), 10)->unitId();
		const auto own = side == BattleSide::ALL_KNOWING ? BattleSide::ATTACKER : side;
		const auto enemy = own == BattleSide::ATTACKER ? BattleSide::DEFENDER : BattleSide::ATTACKER;
		addColumn(1, 8, own, true, true, 0);
		addColumn(2, 7, own, false, false, 3);
		addColumn(3, 10, enemy, true, true, 0);
		ASSERT_NO_FATAL_FAILURE(capture(side));
	}

	Units units(const std::shared_ptr<HypotheticBattle> & model)
	{
		Units result;
		for(const auto * unit : model->battleGetAllUnits(false))
		{
			const auto owned = model->getForUpdate(unit->unitId());
			std::vector<JsonNode> bonuses;
			const auto records = owned->getUnstackedBonuses(Selector::all);
			for(const auto & bonus : *records)
				bonuses.push_back(bonus->toJsonNode());
			result.emplace(unit->unitId(), std::pair(owned->save(), bonuses));
		}
		return result;
	}

	std::vector<JsonNode> obstacles(const std::shared_ptr<HypotheticBattle> & model)
	{
		std::vector<JsonNode> result;
		for(const auto & obstacle : model->getAllObstacles())
		{
			auto copy = *std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle);
			ObstacleChanges change;
			copy.toInfo(change);
			result.push_back(change.data);
		}
		return result;
	}

	// Independent policy oracle: explicit observer and original enemy-turn formula.
	ReachabilityInfo expected(const battle::Unit * actor, DamageCache & damage)
	{
		ReachabilityInfo::Parameters params(actor, actor->getPosition());
		if(observer != BattleSide::ALL_KNOWING)
			params.perspective = observer;
		const auto controller = child->playerToSide(child->battleGetOwner(actor));
		if(controller == BattleSide::ATTACKER || controller == BattleSide::DEFENDER)
			params.ghostWalk = newHorizonsShroud::rank(child->battleGetFightingHero(controller)) > 0;
		child->configurePassingLines(actor, params);
		if(!params.flying && !params.ghostWalk)
		{
			for(const auto * other : child->battleAliveUnits())
			{
				const auto first = child->battleGetOwner(actor);
				const auto second = child->battleGetOwner(other);
				if(first == PlayerColor::CANNOT_DETERMINE || second == PlayerColor::CANNOT_DETERMINE || first == second)
					continue;
				const auto dealt = damage.getOriginalDamage(actor, other, child);
				const auto turns = std::min<int64_t>(100, other->getAvailableHealth() / std::max<int64_t>(dealt, 1));
				for(const auto hex : other->getHexes())
					if(hex.isAvailable())
						params.destructibleEnemyTurns[hex.toInt()] = turns * other->getMovementRange();
			}
			params.bypassEnemyStacks = true;
		}
		return child->getReachability(params);
	}

	void compare(const ReachabilityInfo & actual, const ReachabilityInfo & oracle)
	{
		EXPECT_EQ(actual.distances, oracle.distances);
		EXPECT_EQ(actual.predecessors, oracle.predecessors);
		EXPECT_EQ(static_cast<const TAccessibilityArray &>(actual.accessibility),
			static_cast<const TAccessibilityArray &>(oracle.accessibility));
		EXPECT_EQ(actual.accessibility.demonicGateReservationCounts, oracle.accessibility.demonicGateReservationCounts);
		EXPECT_EQ(actual.accessibility.demonicGateReservationBase, oracle.accessibility.demonicGateReservationBase);
		ASSERT_EQ(bool(actual.accessibility.destructibleEnemyTurns), bool(oracle.accessibility.destructibleEnemyTurns));
		if(actual.accessibility.destructibleEnemyTurns)
			EXPECT_EQ(*actual.accessibility.destructibleEnemyTurns, *oracle.accessibility.destructibleEnemyTurns);
		const auto & a = actual.params;
		const auto & b = oracle.params;
		EXPECT_EQ(a.side, b.side);
		EXPECT_EQ(a.perspective, b.perspective);
		EXPECT_EQ(a.doubleWide, b.doubleWide);
		EXPECT_EQ(a.flying, b.flying);
		EXPECT_EQ(a.passThrough, b.passThrough);
		EXPECT_EQ(a.ignoreKnownAccessible, b.ignoreKnownAccessible);
		EXPECT_EQ(a.bypassEnemyStacks, b.bypassEnemyStacks);
		EXPECT_EQ(a.ghostWalk, b.ghostWalk);
		EXPECT_EQ(a.friendlyTransit, b.friendlyTransit);
		EXPECT_EQ(a.destructibleEnemyTurns, b.destructibleEnemyTurns);
		EXPECT_EQ(a.startPosition, b.startPosition);
		ASSERT_NE(a.knownAccessible, nullptr);
		ASSERT_NE(b.knownAccessible, nullptr);
		EXPECT_EQ(*a.knownAccessible, *b.knownAccessible);
	}

	ReachabilityInfo check(uint32_t id)
	{
		// Ordinary serialized retaliation-cache materialization precedes baseline.
		for(const auto * unit : battle()->battleGetAllUnits(false))
			(void)dynamic_cast<const battle::CUnitState &>(*unit).counterAttacks.total();
		for(const auto & model : {parent, child})
			for(const auto * unit : model->battleGetAllUnits(false))
				model->getForUpdate(unit->unitId())->counterAttacks.total();
		const auto parentBefore = units(parent);
		const auto childBefore = units(child);
		const auto parentObstacles = obstacles(parent);
		const auto childObstacles = obstacles(child);
		std::map<uint32_t, JsonNode> liveBefore;
		for(const auto * unit : battle()->battleGetAllUnits(false))
			liveBefore.emplace(unit->unitId(), unit->acquireState()->save());
		const auto worldBefore = gameState()->saveToMemory();
		const auto battleBefore = projectedHitTest::serialized(*battle());
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto aiRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
		const auto * actor = child->battleGetUnitByID(id);
		DamageCache original;
		DamageCache actualCache(&original);
		DamageCache oracleCache(&original);
		const auto actual = getReachabilityWithEnemyBypass(actor, actualCache, child);
		const auto oracle = expected(actor, oracleCache);
		compare(actual, oracle);
		EXPECT_EQ(actual.params.side, actor->unitSide());
		EXPECT_EQ(actual.params.perspective, observer == BattleSide::ALL_KNOWING ? actor->unitSide() : observer);
		EXPECT_EQ(StopperProbe(*child).getStoppers(actual.params.perspective), StopperProbe(*child).getStoppers(oracle.params.perspective));
		EXPECT_EQ(units(parent), parentBefore);
		EXPECT_EQ(units(child), childBefore);
		EXPECT_EQ(obstacles(parent), parentObstacles);
		EXPECT_EQ(obstacles(child), childObstacles);
		for(const auto & [unitId, saved] : liveBefore)
			EXPECT_EQ(battle()->battleGetUnitByID(unitId)->acquireState()->save(), saved);
		EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
		EXPECT_EQ(projectedHitTest::serialized(*battle()), battleBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), aiRng);
		return actual;
	}

	void grounded()
	{
		for(const auto id : {leftId, rightId})
		{
			const auto result = check(id);
			const auto start = child->battleGetUnitByID(id)->getPosition();
			const auto costly = BattleHex(7, start.getY());
			EXPECT_EQ(result.distances[costly.toInt()], 4u);
			EXPECT_TRUE(result.params.bypassEnemyStacks);
			EXPECT_TRUE(StopperProbe(*child).getStoppers(result.params.perspective).contains(BattleHex(8, 5)));
			EXPECT_FALSE(result.isReachable(BattleHex(9, start.getY())));
			EXPECT_FALSE(StopperProbe(*child).getStoppers(result.params.perspective).contains(BattleHex(10, 5)));
		}
	}
};

TEST_F(EnemyBypassReachabilityTest, AttackerObserverOwnAndEnemyGroundedPaths)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ATTACKER));
	grounded();
}

TEST_F(EnemyBypassReachabilityTest, DefenderObserverOwnAndEnemyGroundedPaths)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::DEFENDER));
	grounded();
}

TEST_F(EnemyBypassReachabilityTest, AllKnowingRetainsPerUnitPerspective)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ALL_KNOWING));
	for(const auto id : {leftId, rightId})
	{
		const auto result = check(id);
		const auto own = id == leftId;
		EXPECT_EQ(StopperProbe(*child).getStoppers(result.params.perspective).contains(BattleHex(8, 5)), own);
		EXPECT_EQ(StopperProbe(*child).getStoppers(result.params.perspective).contains(BattleHex(10, 5)), !own);
	}
}

TEST_F(EnemyBypassReachabilityTest, FlyingEnemyRetainsObserverPerspectiveWithoutBypass)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ATTACKER, true));
	const auto result = check(rightId);
	EXPECT_TRUE(result.params.flying);
	EXPECT_FALSE(result.params.bypassEnemyStacks);
	EXPECT_TRUE(result.isReachable(BattleHex(9, 7)));
}

TEST_F(EnemyBypassReachabilityTest, GhostWalkFollowsVisibleHypnotizedControllerNotNominalSide)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ATTACKER));
	const SecondarySkill shroud(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID)));
	attackerSideHero->setSecSkillLevel(shroud, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(capture(BattleSide::ATTACKER));
	auto actor = child->getForUpdate(rightId);
	actor->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_EQ(child->battleGetOwner(actor.get()), attackerSideHero->getOwner());
	const auto result = check(rightId);
	EXPECT_TRUE(result.params.ghostWalk);
	EXPECT_FALSE(result.params.bypassEnemyStacks);
	EXPECT_EQ(result.params.side, BattleSide::DEFENDER);
}

TEST_F(EnemyBypassReachabilityTest, PassingLinesPreservesFriendlyTransitAndEnemyTurnWeights)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ATTACKER));
	const std::string skill = "new-horizons:battlecraft";
	const SecondarySkill id(SecondarySkill::decode(skill));
	for(const auto & [rank, perk] : std::vector<std::pair<MasteryLevel::Type, std::string>>{
		{MasteryLevel::BASIC, "new-horizons:battlecraft.entrench"},
		{MasteryLevel::ADVANCED, "new-horizons:battlecraft.passingLines"}})
	{
		attackerSideHero->setSecSkillLevel(id, rank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(skill, perk, attackerSideHero->getPerkSkillRank(skill)));
		attackerSideHero->applyPerkSelection({skill, perk});
	}
	const auto friendId = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 10)->unitId();
	ASSERT_NO_FATAL_FAILURE(capture(BattleSide::ATTACKER));
	const auto result = check(leftId);
	EXPECT_TRUE(result.params.friendlyTransit.test(child->battleGetUnitByID(friendId)->getPosition().toInt()));
	EXPECT_TRUE(result.params.bypassEnemyStacks);
	EXPECT_GE(result.params.destructibleEnemyTurns[child->battleGetUnitByID(rightId)->getPosition().toInt()], 0);
}

TEST_F(EnemyBypassReachabilityTest, NestedSnapshotNeverDiscoversEnemyHiddenObstacle)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::DEFENDER));
	const auto parentView = parent->getAllObstacles();
	const auto childView = child->getAllObstacles();
	const auto result = check(leftId);
	EXPECT_EQ(parent->getAllObstacles(), parentView);
	EXPECT_EQ(child->getAllObstacles(), childView);
	EXPECT_FALSE(StopperProbe(*child).getStoppers(result.params.perspective).contains(BattleHex(10, 5)));
}
}
