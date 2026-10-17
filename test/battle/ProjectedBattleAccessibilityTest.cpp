/*
 * ProjectedBattleAccessibilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../bonus/ProjectedUnitEnvironmentFixture.h"
#include "../../lib/battle/ProjectedBattleAccessibility.h"
#include "../../lib/battle/LiveBattleAccessibilityContext.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include <stdexcept>

namespace test
{
using namespace ::testing;
namespace
{
class TilesObstacle final : public CObstacleInstance
{
public:
	BattleHexArray tiles;
	bool rejectRead = false;
	bool blocksTiles() const override { return true; }
	BattleHexArray getAffectedTiles() const override
	{
		if(rejectRead)
			throw std::runtime_error("synthetic obstacle capture refusal");
		return tiles;
	}
	bool visibleForSide(BattleSide, bool) const override { return false; }
};

class ReservationsSource : public environmentFixture::Source
{
public:
	BattleSideArray<std::vector<PendingDemonicGateFootprint>> pending;
	std::vector<PendingDemonicGateFootprint> getPendingDemonicGateFootprints(BattleSide side) const override
	{
		return pending.at(side);
	}
};

void expectSameMap(const AccessibilityInfo & first, const AccessibilityInfo & second)
{
	EXPECT_EQ(static_cast<const TAccessibilityArray &>(first), static_cast<const TAccessibilityArray &>(second));
	EXPECT_EQ(first.demonicGateReservationCounts, second.demonicGateReservationCounts);
	EXPECT_EQ(first.demonicGateReservationBase, second.demonicGateReservationBase);
	EXPECT_EQ(first.destructibleEnemyTurns, second.destructibleEnemyTurns);
}
}

class ProjectedBattleAccessibilityTest : public Test, protected environmentFixture::Projection
{
protected:
	void SetUp() override
	{
		ON_CALL(source, getBattlefieldType()).WillByDefault(Return(BattleField::NONE));
		initialize();
	}
	battle::BattleAccessibilityState capture() const
	{
		return battle::BattleAccessibilityState::capture(callback);
	}
	AccessibilityInfo query(const battle::BattleAccessibilityState & state) const
	{
		return battle::buildBattleAccessibility(battle::ProjectedBattleAccessibilityContext(state, *roster, graph));
	}
};

TEST_F(ProjectedBattleAccessibilityTest, CapturedLiveParityComparesEveryTileAndReservationField)
{
	const auto state = capture();
	expectSameMap(query(state), callback.getAccessibility());
	EXPECT_EQ(query(state)[BattleHex(5, 5).toInt()], EAccessibility::ALIVE_STACK);
	EXPECT_EQ(query(state)[BattleHex(0, 5).toInt()], EAccessibility::SIDE_COLUMN);
	EXPECT_EQ(query(state)[BattleHex(8, 5).toInt()], EAccessibility::ACCESSIBLE);
}

TEST_F(ProjectedBattleAccessibilityTest, EarlierProjectedMoveFreesOldFootprintAndBlocksNewWithoutLiveFallback)
{
	const auto state = capture();
	units[0]->position = BattleHex(8, 5);
	const auto projected = query(state);
	EXPECT_EQ(projected[BattleHex(5, 5).toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_EQ(projected[BattleHex(8, 5).toInt()], EAccessibility::ALIVE_STACK);
	EXPECT_EQ(callback.getAccessibility()[BattleHex(5, 5).toInt()], EAccessibility::ALIVE_STACK);
	EXPECT_EQ(subject.getPosition(), BattleHex(5, 5));
}

TEST_F(ProjectedBattleAccessibilityTest, OccupiedCorpsePlacementUsesLatestRosterBeforeAnyIncomingLoad)
{
	const auto state = capture();
	units[0]->health.reset();
	units[3]->position = units[0]->getPosition();
	EXPECT_FALSE(query(state).accessible(units[0]->getPosition(), units[0].get()));
	units[3]->position = BattleHex(6, 5);
	EXPECT_TRUE(query(state).accessible(units[0]->getPosition(), units[0].get()));
	EXPECT_TRUE(subject.alive());
}

TEST_F(ProjectedBattleAccessibilityTest, DeathAndGhostRetainExactValidTargetPredicateWithoutExtraGhostFilter)
{
	const auto state = capture();
	units[0]->health.reset();
	EXPECT_EQ(query(state)[BattleHex(5, 5).toInt()], EAccessibility::ACCESSIBLE);
	units[3]->ghost = true;
	ASSERT_TRUE(units[3]->alive());
	EXPECT_EQ(query(state)[BattleHex(6, 5).toInt()], EAccessibility::ALIVE_STACK);
	EXPECT_EQ(callback.getAccessibility()[BattleHex(6, 5).toInt()], EAccessibility::ALIVE_STACK);
}

TEST_F(ProjectedBattleAccessibilityTest, ProjectedTimeStopRetainsExistingValidTargetExclusionFromOccupancy)
{
	const auto state = capture();
	graph.addExported(supporter, environmentFixture::bonus(BonusType::TIME_STOP, 1));
	ASSERT_TRUE(units[3]->alive());
	ASSERT_TRUE(units[3]->isTimeStopped());
	EXPECT_FALSE(units[3]->isValidTarget(false));
	EXPECT_EQ(query(state)[BattleHex(6, 5).toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_EQ(callback.getAccessibility()[BattleHex(6, 5).toInt()], EAccessibility::ALIVE_STACK);
}

TEST_F(ProjectedBattleAccessibilityTest, SynchronousProjectedFormChangesCurrentFootprintThenRestoresIt)
{
	ASSERT_TRUE(CreatureID(10).toCreature()->isDoubleWide());
	const auto state = capture();
	ASSERT_FALSE(units[0]->doubleWide());
	units[0]->beginBattleForm(CreatureID(10), 2);
	ASSERT_TRUE(units[0]->doubleWide());
	EXPECT_EQ(query(state)[BattleHex(4, 5).toInt()], EAccessibility::ALIVE_STACK);
	units[0]->endBattleForm();
	EXPECT_EQ(query(state)[BattleHex(4, 5).toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_FALSE(subject.hasBattleForm());
}

TEST_F(ProjectedBattleAccessibilityTest, SideColumnOrInvalidTurretFootprintsDoNotOverwriteBoundary)
{
	const auto state = capture();
	units[0]->position = BattleHex(0, 5);
	EXPECT_EQ(query(state)[BattleHex(0, 5).toInt()], EAccessibility::SIDE_COLUMN);
	units[0]->position = BattleHex::INVALID;
	EXPECT_EQ(query(state)[BattleHex(5, 5).toInt()], EAccessibility::ACCESSIBLE);
}

TEST_F(ProjectedBattleAccessibilityTest, ObstacleTilesAreOwnedAndDoNotFollowLaterSourceChanges)
{
	auto obstacle = std::make_shared<TilesObstacle>();
	obstacle->tiles = BattleHexArray{BattleHex(8, 5)};
	ON_CALL(source, getAllObstacles()).WillByDefault(Return(IBattleInfo::ObstacleCList{obstacle}));
	const auto state = capture(); // ALL_KNOWING sees even this hidden obstacle
	EXPECT_EQ(query(state)[BattleHex(8, 5).toInt()], EAccessibility::OBSTACLE);
	obstacle->tiles = BattleHexArray{BattleHex(9, 5)};
	EXPECT_EQ(query(state)[BattleHex(8, 5).toInt()], EAccessibility::OBSTACLE);
	EXPECT_EQ(query(state)[BattleHex(9, 5).toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_EQ(callback.getAccessibility()[BattleHex(9, 5).toInt()], EAccessibility::OBSTACLE);
}

TEST_F(ProjectedBattleAccessibilityTest, OrderedPrivateObstacleReplacementIsVisibleOnNextQuery)
{
	auto state = capture();
	state.obstacles = {BattleHexArray{BattleHex(8, 5)}};
	EXPECT_EQ(query(state)[BattleHex(8, 5).toInt()], EAccessibility::OBSTACLE);
	state.obstacles = {BattleHexArray{BattleHex(9, 5)}};
	EXPECT_EQ(query(state)[BattleHex(8, 5).toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_EQ(query(state)[BattleHex(9, 5).toInt()], EAccessibility::OBSTACLE);
}

TEST_F(ProjectedBattleAccessibilityTest, GateAndWallInputsRespectOriginalPlacementAndPrecedence)
{
	auto state = capture();
	state.walls = true;
	state.gate = EGateState::CLOSED;
	for(const auto & part : battle::accessibilityWallParts())
		state.wallStates.at(static_cast<size_t>(part.first)) = EWallState::INTACT;
	auto map = query(state);
	EXPECT_EQ(map[BattleHex::GATE_OUTER], EAccessibility::GATE);
	EXPECT_FALSE(map.accessible(BattleHex(BattleHex::GATE_OUTER), false, BattleSide::ATTACKER));
	EXPECT_TRUE(map.accessible(BattleHex(BattleHex::GATE_OUTER), false, BattleSide::DEFENDER));
	for(const auto & part : battle::accessibilityWallParts())
		EXPECT_EQ(map[part.second.toInt()], EAccessibility::DESTRUCTIBLE_WALL);
	state.gate = EGateState::BLOCKED;
	EXPECT_EQ(query(state)[BattleHex::GATE_OUTER], EAccessibility::UNAVAILABLE);
	state.gate = EGateState::DESTROYED;
	EXPECT_EQ(query(state)[BattleHex::GATE_OUTER], EAccessibility::ACCESSIBLE);
}

TEST_F(ProjectedBattleAccessibilityTest, WallsAfterObstacleAfterUnitAfterGateAfterImpassable)
{
	auto state = capture();
	const auto wall = battle::accessibilityWallParts().front();
	state.walls = true;
	state.gate = EGateState::CLOSED;
	state.impassable = BattleHexArray{BattleHex(8, 5), BattleHex(BattleHex::GATE_OUTER)};
	state.obstacles = {BattleHexArray{wall.second, BattleHex(8, 5), BattleHex(BattleHex::GATE_OUTER)}};
	state.wallStates.at(static_cast<size_t>(wall.first)) = EWallState::INTACT;
	units[0]->position = BattleHex(8, 5);
	auto map = query(state);
	EXPECT_EQ(map[BattleHex(8, 5).toInt()], EAccessibility::OBSTACLE);
	EXPECT_EQ(map[wall.second.toInt()], EAccessibility::DESTRUCTIBLE_WALL);
	EXPECT_EQ(map[BattleHex::GATE_OUTER], EAccessibility::OBSTACLE);
	state.obstacles.clear();
	EXPECT_EQ(query(state)[BattleHex::GATE_OUTER], EAccessibility::GATE);
	state.walls = false;
	EXPECT_EQ(query(state)[BattleHex::GATE_OUTER], EAccessibility::UNAVAILABLE);
}

TEST_F(ProjectedBattleAccessibilityTest, OverlappingGateReservationsRebuildCountsAndReleaseIndependently)
{
	auto state = capture();
	ASSERT_TRUE(CreatureID(10).toCreature()->isDoubleWide());
	state.reservations.at(BattleSide::ATTACKER) = {{CreatureID(10), BattleHex(9, 5)}};
	state.reservations.at(BattleSide::DEFENDER) = {{CreatureID(0), BattleHex(9, 5)}};
	auto map = query(state);
	EXPECT_EQ(map.demonicGateReservationCounts[BattleHex(9, 5).toInt()], 2u);
	EXPECT_EQ(map.demonicGateReservationCounts[BattleHex(8, 5).toInt()], 1u);
	EXPECT_FALSE(map.accessibleForDemonicGateArrival(BattleHex(9, 5), true,
		BattleSide::ATTACKER, BattleHex(9, 5), true));
	state.reservations.at(BattleSide::DEFENDER).clear();
	EXPECT_TRUE(query(state).accessibleForDemonicGateArrival(BattleHex(9, 5), true,
		BattleSide::ATTACKER, BattleHex(9, 5), true));
	state.reservations.at(BattleSide::ATTACKER).clear();
	EXPECT_FALSE(query(state).isDemonicGateReserved(BattleHex(9, 5)));
}

TEST_F(ProjectedBattleAccessibilityTest, ReservationKeepsUnderlyingObstacleAndExplicitOverrideCannotEraseIt)
{
	auto state = capture();
	state.obstacles = {BattleHexArray{BattleHex(9, 5)}};
	state.reservations.at(BattleSide::ATTACKER) = {{CreatureID(0), BattleHex(9, 5)}};
	const auto map = battle::withAccessibleHexes(query(state), BattleHexArray{BattleHex(9, 5)});
	EXPECT_TRUE(map.isDemonicGateReserved(BattleHex(9, 5)));
	EXPECT_EQ(map.demonicGateReservationBase[BattleHex(9, 5).toInt()], EAccessibility::OBSTACLE);
	EXPECT_FALSE(map.accessibleForDemonicGateArrival(BattleHex(9, 5), false,
		BattleSide::ATTACKER, BattleHex(9, 5), false));
}

TEST_F(ProjectedBattleAccessibilityTest, ExplicitKnownAccessibleOverrideRetainsLegacyNonreservationBehavior)
{
	auto state = capture();
	state.obstacles = {BattleHexArray{BattleHex(8, 5)}};
	const BattleHexArray allow{BattleHex(8, 5), BattleHex(5, 5)};
	auto map = battle::withAccessibleHexes(query(state), allow);
	EXPECT_EQ(map[BattleHex(8, 5).toInt()], EAccessibility::ACCESSIBLE);
	EXPECT_EQ(map[BattleHex(5, 5).toInt()], EAccessibility::ACCESSIBLE);
	expectSameMap(battle::withAccessibleHexes(callback.getAccessibility(), allow), callback.getAccessibility(allow));
}

TEST_F(ProjectedBattleAccessibilityTest, PendingReservationCaptureIsOwnedIncludingIgnoredEmptyCreature)
{
	NiceMock<ReservationsSource> original;
	ON_CALL(original, getBattlefieldType()).WillByDefault(Return(BattleField::NONE));
	original.pending.at(BattleSide::ATTACKER) = {{CreatureID(0), BattleHex(9, 5)}, {CreatureID::NONE, BattleHex(8, 5)}};
	environmentFixture::Callback view(original);
	const auto state = battle::BattleAccessibilityState::capture(view);
	original.pending.at(BattleSide::ATTACKER).clear();
	EXPECT_TRUE(query(state).isDemonicGateReserved(BattleHex(9, 5)));
	EXPECT_FALSE(query(state).isDemonicGateReserved(BattleHex(8, 5)));
	EXPECT_FALSE(view.getAccessibility().isDemonicGateReserved(BattleHex(9, 5)));
}

TEST_F(ProjectedBattleAccessibilityTest, CaptureRefusesAbsentOrHiddenViewBeforeObstacleMetadata)
{
	CPlayerBattleCallback hidden(&source, PlayerColor(0));
	EXPECT_CALL(source, getAllObstacles()).Times(0);
	EXPECT_THROW(battle::BattleAccessibilityState::capture(hidden), std::invalid_argument);
	CPlayerBattleCallback absent(nullptr, PlayerColor(0));
	EXPECT_THROW(battle::BattleAccessibilityState::capture(absent), std::invalid_argument);
}

TEST_F(ProjectedBattleAccessibilityTest, CaptureFailureDoesNotMutateValuesGraphOrGeneration)
{
	auto obstacle = std::make_shared<TilesObstacle>();
	obstacle->rejectRead = true;
	ON_CALL(source, getAllObstacles()).WillByDefault(Return(IBattleInfo::ObstacleCList{obstacle}));
	const auto before = units[0]->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	EXPECT_THROW(capture(), std::runtime_error);
	EXPECT_EQ(units[0]->save(), before); // exact projected provider, not ordinary detached
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(ProjectedBattleAccessibilityTest, ReplacedProviderRefusesBeforeAnyLatestTargetQuery)
{
	const auto state = capture();
	battle::CUnitStateDetached ordinary(&supporter, &supporter);
	battle::UnitStateValueFields::copy(ordinary, supporter);
	*units[3] = ordinary;
	EXPECT_THROW(query(state), std::invalid_argument);
}
}
