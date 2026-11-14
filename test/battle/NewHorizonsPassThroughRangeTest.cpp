/*
 * NewHorizonsPassThroughRangeTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/battle/AccessibilityInfo.h"
#include "../../lib/battle/BattleReach.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"

#include "../mock/mock_BonusBearer.h"
#include "../mock/mock_battle_Unit.h"

#include <limits>

using namespace battle;
using namespace testing;

namespace
{

class TestBattleCallback final : public CBattleInfoCallback
{
public:
	const IBattleInfo * getBattle() const override
	{
		return nullptr;
	}

	std::optional<PlayerColor> getPlayerID() const override
	{
		return std::nullopt;
	}
};

class TestUnit final : public UnitMock
{
	BonusBearerMock bonuses;

public:
	TestUnit(BattleSide side, BattleHex position, bool doubleWide = false)
	{
		ON_CALL(*this, getAllBonuses(_, _)).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getAllBonuses));
		ON_CALL(*this, getTreeVersion()).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getTreeVersion));
		ON_CALL(*this, unitSide()).WillByDefault(Return(side));
		ON_CALL(*this, getPosition()).WillByDefault(Return(position));
		ON_CALL(*this, doubleWide()).WillByDefault(Return(doubleWide));
		ON_CALL(*this, isInvincible()).WillByDefault(Return(false));
	}

	void addLongReach(int32_t gapHexes)
	{
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LONG_REACH,
			BonusSource::CREATURE_ABILITY, gapHexes, CreatureID(0)));
	}
};

} // namespace

TEST(AccessibilityInfoPassThroughTest, TraversalAllowlistExcludesUnpassableBattlefieldTilesAndReservations)
{
	AccessibilityInfo accessibility;
	accessibility.fill(EAccessibility::ACCESSIBLE);

	const BattleHex stack(60);
	const BattleHex obstacle(61);
	const BattleHex wall(62);
	const BattleHex closedGate(BattleHex::GATE_OUTER);
	const BattleHex specialField(63);
	const BattleHex sideColumn(0, 5);
	const BattleHex reserved(64);
	accessibility[stack.toInt()] = EAccessibility::ALIVE_STACK;
	accessibility[obstacle.toInt()] = EAccessibility::OBSTACLE;
	accessibility[wall.toInt()] = EAccessibility::DESTRUCTIBLE_WALL;
	accessibility[closedGate.toInt()] = EAccessibility::GATE;
	accessibility[specialField.toInt()] = EAccessibility::UNAVAILABLE;
	accessibility[sideColumn.toInt()] = EAccessibility::SIDE_COLUMN;
	accessibility.reserveDemonicGateFootprint(reserved, false, BattleSide::ATTACKER);

	for(const BattleHex & passable : {stack, obstacle, wall, closedGate})
	{
		EXPECT_TRUE(accessibility.accessibleForPassThroughTransit(passable, false, BattleSide::ATTACKER));
		EXPECT_TRUE(accessibility.accessibleForPassThroughTransit(passable, false, BattleSide::DEFENDER));
	}
	for(const BattleHex & blocked : {specialField, sideColumn, reserved})
		EXPECT_FALSE(accessibility.accessibleForPassThroughTransit(blocked, false, BattleSide::ATTACKER));
}

TEST(AccessibilityInfoPassThroughTest, DestinationMustRemainNormallyLegalAndUnoccupied)
{
	AccessibilityInfo accessibility;
	accessibility.fill(EAccessibility::ACCESSIBLE);
	auto destructibleForecast = std::make_shared<TBattlefieldTurnsArray>();
	destructibleForecast->fill(0);
	accessibility.destructibleEnemyTurns = destructibleForecast;

	const BattleHex occupied(60);
	const BattleHex obstacle(61);
	const BattleHex closedGate(BattleHex::GATE_OUTER);
	accessibility[occupied.toInt()] = EAccessibility::ALIVE_STACK;
	accessibility[obstacle.toInt()] = EAccessibility::OBSTACLE;
	accessibility[closedGate.toInt()] = EAccessibility::GATE;

	EXPECT_TRUE(accessibility.accessible(occupied, false, BattleSide::ATTACKER));
	EXPECT_FALSE(accessibility.accessibleForMovementEndpoint(occupied, false, BattleSide::ATTACKER));
	EXPECT_FALSE(accessibility.accessibleForMovementEndpoint(obstacle, false, BattleSide::ATTACKER));
	EXPECT_FALSE(accessibility.accessibleForMovementEndpoint(closedGate, false, BattleSide::ATTACKER));
	EXPECT_TRUE(accessibility.accessibleForMovementEndpoint(closedGate, false, BattleSide::DEFENDER));

	const BattleHex doubleWidePosition(5, 5);
	const auto occupiedFootprint = Unit::getHexes(doubleWidePosition, true, BattleSide::ATTACKER);
	ASSERT_EQ(2, occupiedFootprint.size());
	accessibility[occupiedFootprint[1].toInt()] = EAccessibility::OBSTACLE;
	EXPECT_TRUE(accessibility.accessibleForPassThroughTransit(doubleWidePosition, true, BattleSide::ATTACKER));
	EXPECT_FALSE(accessibility.accessibleForMovementEndpoint(doubleWidePosition, true, BattleSide::ATTACKER));
}

TEST(ReachabilityInfoPassThroughTest, BlockedTransitTilesCannotBecomeDestinations)
{
	ReachabilityInfo reachability;
	reachability.params.passThrough = true;
	reachability.params.side = BattleSide::ATTACKER;
	reachability.accessibility.fill(EAccessibility::ACCESSIBLE);

	const BattleHex blocked(60);
	reachability.accessibility[blocked.toInt()] = EAccessibility::ALIVE_STACK;
	reachability.distances[blocked.toInt()] = 2;

	EXPECT_FALSE(reachability.isReachable(blocked));
}

TEST(AccessibilityInfoLongReachLineTest, ClearCorridorRejectsIntermediateBlockers)
{
	AccessibilityInfo accessibility;
	accessibility.fill(EAccessibility::ACCESSIBLE);
	const BattleHex from(5, 5);
	const BattleHex to(9, 5);
	const BattleHex middle(7, 5);
	const BattleHexArray endpoints{from, to};

	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, endpoints));
	accessibility[middle.toInt()] = EAccessibility::OBSTACLE;
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints));

	accessibility[middle.toInt()] = EAccessibility::ALIVE_STACK;
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints));
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, BattleHexArray{from, to, middle}));
}

TEST(AccessibilityInfoLongReachLineTest, AnyClearTiedStraightRayIsAccepted)
{
	AccessibilityInfo accessibility;
	accessibility.fill(EAccessibility::ACCESSIBLE);
	const BattleHex from(5, 5);
	const BattleHex to(8, 6);
	const BattleHexArray endpoints{from, to};

	// At the middle interpolation layer this ray has two tied cells: (7, 5)
	// and (6, 6). They share the earlier (6, 5) step, so block one branch only.
	accessibility[BattleHex(7, 5).toInt()] = EAccessibility::OBSTACLE;
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, endpoints));

	accessibility[BattleHex(6, 6).toInt()] = EAccessibility::DESTRUCTIBLE_WALL;
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints));
}

TEST(CBattleInfoCallbackLongReachTest, GapCountExtendsMeleeRangeWithoutChangingAdjacencyOnlyQueries)
{
	TestBattleCallback callback;
	TestUnit attacker(BattleSide::ATTACKER, BattleHex(5, 5));
	TestUnit defender(BattleSide::DEFENDER, BattleHex(11, 5));
	attacker.addLongReach(5);

	EXPECT_FALSE(callback.isMeleeAttackPossible(&attacker, &defender));
	EXPECT_TRUE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &defender));

	TestUnit outsideRange(BattleSide::DEFENDER, BattleHex(12, 5));
	EXPECT_FALSE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &outsideRange));

	TestUnit adjacent(BattleSide::DEFENDER, BattleHex(6, 5));
	EXPECT_TRUE(callback.isMeleeAttackPossible(&attacker, &adjacent));
	EXPECT_TRUE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &adjacent));
}

TEST(AccessibilityInfoLongReachLineTest, RequestedApproachUsesTheActualClearFinalEdgeOfATiedRay)
{
	AccessibilityInfo accessibility;
	accessibility.fill(EAccessibility::ACCESSIBLE);
	const BattleHex from(5, 5);
	const BattleHex to(2, 3);
	const BattleHex right(3, 3);
	const BattleHex bottomRight(2, 4);
	const BattleHexArray endpoints{from, to};
	ASSERT_EQ(BattleHex::getDistance(from, to), 4);
	for(const auto direction : {BattleHex::RIGHT, BattleHex::BOTTOM_RIGHT})
		EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, endpoints, direction));

	accessibility[right.toInt()] = EAccessibility::OBSTACLE;
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, endpoints));
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints, BattleHex::RIGHT));
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, endpoints, BattleHex::BOTTOM_RIGHT));
	accessibility[right.toInt()] = EAccessibility::ACCESSIBLE;
	accessibility[bottomRight.toInt()] = EAccessibility::ALIVE_STACK;
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, endpoints));
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, to, endpoints, BattleHex::RIGHT));
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints, BattleHex::BOTTOM_RIGHT));
	accessibility[right.toInt()] = EAccessibility::DESTRUCTIBLE_WALL;
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints));
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints, BattleHex::TOP));
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, to, endpoints, BattleHex::BOTTOM));
}

TEST(CBattleInfoCallbackLongReachTest, MaximumDistanceUsesNetBonusesAndClampsWithoutIntegerOverflow)
{
	TestBattleCallback callback;
	TestUnit attacker(BattleSide::ATTACKER, BattleHex(5, 5));
	EXPECT_EQ(battleReach::maximumMeleeDistance(attacker), 1);
	attacker.addLongReach(5);
	EXPECT_EQ(battleReach::maximumMeleeDistance(attacker), 6);
	attacker.addLongReach(-2);
	EXPECT_EQ(battleReach::maximumMeleeDistance(attacker), 4);
	TestUnit inRange(BattleSide::DEFENDER, BattleHex(9, 5));
	TestUnit outsideRange(BattleSide::DEFENDER, BattleHex(10, 5));
	EXPECT_TRUE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &inRange));
	EXPECT_FALSE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &outsideRange));
	attacker.addLongReach(-10);
	EXPECT_EQ(battleReach::maximumMeleeDistance(attacker), 1);
	EXPECT_FALSE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &inRange));
	EXPECT_EQ(battleReach::maximumMeleeDistance(std::numeric_limits<int64_t>::min()), 1);
	EXPECT_EQ(battleReach::maximumMeleeDistance(std::numeric_limits<int64_t>::max()), std::numeric_limits<int64_t>::max());
}

TEST(AccessibilityInfoLongReachLineTest, DoublewideApproachUsesTheExposedEntryInsteadOfAnInternalBodyEdge)
{
	AccessibilityInfo accessibility;
	accessibility.fill(EAccessibility::ACCESSIBLE);
	const BattleHex from(4, 2);
	const BattleHex nearHalf(8, 5);
	const BattleHex farHalf(9, 5);
	const BattleHexArray footprint{nearHalf, farHalf};
	const BattleHexArray endpoints{from, nearHalf, farHalf};
	for(const auto & occupied : footprint)
		accessibility[occupied.toInt()] = EAccessibility::ALIVE_STACK;
	ASSERT_TRUE(accessibility.hasClearStraightHexRay(from, farHalf, endpoints));
	// A single-cell endpoint still has a final LEFT edge. For a doublewide
	// target that edge is internal: the actual exposed entry was TOP_LEFT.
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, farHalf, endpoints, BattleHex::LEFT));
	EXPECT_FALSE(accessibility.hasClearStraightHexRay(from, farHalf, endpoints, BattleHex::LEFT, footprint));
	EXPECT_TRUE(accessibility.hasClearStraightHexRay(from, farHalf, endpoints, BattleHex::TOP_LEFT, footprint));
}

TEST(AccessibilityInfoLongReachLineTest, FootprintEntrySectorUnionPreservesGenericRaysWithBlockersAndProjectedCells)
{
	const BattleHexArray footprint{BattleHex(8, 5), BattleHex(9, 5)};
	const BattleHexArray liveFootprint{BattleHex(11, 7), BattleHex(12, 7)};
	for(const bool projected : {false, true})
	for(const auto from : {BattleHex(4, 2), BattleHex(12, 2), BattleHex(4, 8), BattleHex(12, 8)})
	for(const auto blocker : {BattleHex(BattleHex::INVALID), BattleHex(5, 3), BattleHex(6, 3), BattleHex(7, 4), BattleHex(10, 4)})
	{
		SCOPED_TRACE(projected);
		SCOPED_TRACE(from.toInt());
		SCOPED_TRACE(blocker.toInt());
		AccessibilityInfo accessibility;
		accessibility.fill(EAccessibility::ACCESSIBLE);
		for(const auto & occupied : projected ? liveFootprint : footprint)
			accessibility[occupied.toInt()] = EAccessibility::ALIVE_STACK;
		if(blocker.isValid()) accessibility[blocker.toInt()] = EAccessibility::OBSTACLE;
		BattleHexArray endpoints{from};
		endpoints.insert(footprint);
		endpoints.insert(liveFootprint);
		for(const auto & to : footprint)
		{
			SCOPED_TRACE(to.toInt());
			const bool generic = accessibility.hasClearStraightHexRay(from, to, endpoints);
			bool anyPhysicalSector = false;
			for(const auto direction : BattleHex::hexagonalDirections())
				anyPhysicalSector |= accessibility.hasClearStraightHexRay(from, to, endpoints, direction, footprint);
			EXPECT_EQ(anyPhysicalSector, generic)
				<< "Directional classification must not remove a formerly legal ray to either target half";
		}
	}
}

TEST(CBattleInfoCallbackLongReachTest, DoublewideRangeUsesTheNearestOccupiedFootprints)
{
	TestBattleCallback callback;
	TestUnit attacker(BattleSide::ATTACKER, BattleHex(11, 5), true);
	TestUnit defender(BattleSide::DEFENDER, BattleHex(4, 5), true);
	attacker.addLongReach(4);
	ASSERT_EQ(BattleHex::getDistance(attacker.getPosition(), defender.getPosition()), 7);
	EXPECT_TRUE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &defender))
		<< "The nearer occupied cells are five hexes apart";
	attacker.addLongReach(-1);
	EXPECT_FALSE(callback.isMeleeAttackPossibleWithLongReach(&attacker, &defender));
}

TEST(CBattleInfoCallbackLongReachTest, DescriptionDisplaysTheSharedMaximumRatherThanRawGap)
{
	const auto description = [](int maximum)
	{
		return "Reach " + std::to_string(maximum) + ": Can attack in melee up to "
			+ std::to_string(maximum) + " hexes away along a clear straight corridor.";
	};
	auto bonus = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LONG_REACH,
		BonusSource::CREATURE_ABILITY, 5, CreatureID(0));
	EXPECT_EQ(LIBRARY->bth->bonusToString(bonus), description(6));
	TestUnit attacker(BattleSide::ATTACKER, BattleHex(5, 5));
	attacker.addLongReach(5);
	attacker.addLongReach(-2);
	EXPECT_EQ(LIBRARY->bth->bonusToString(bonus, &attacker), description(4));
	attacker.addLongReach(-10);
	EXPECT_EQ(LIBRARY->bth->bonusToString(bonus, &attacker), description(1));
	EXPECT_EQ(LIBRARY->bth->bonusToString(bonus), description(6));
}

TEST(CreatureTransitReachSerializationTest, StaticCapabilitiesRoundTripAndRejectLossyOlderWriters)
{
	for(const auto [type, value] : {std::pair{BonusType::PASS_THROUGH, 0}, std::pair{BonusType::LONG_REACH, 5}})
	{
		Bonus original(BonusDuration::PERMANENT, type, BonusSource::CREATURE_ABILITY,
			value, BonusSourceID());
		CMemorySerializer writer;
		writer.oser.version = ESerializationVersion::CURRENT;
		ASSERT_NO_THROW(writer.oser & original);

		CMemorySerializer reader(writer.extractBuffer());
		reader.iser.version = ESerializationVersion::CURRENT;
		Bonus restored;
		ASSERT_NO_THROW(reader.iser & restored);
		EXPECT_EQ(restored.type, type);
		EXPECT_EQ(restored.val, value);

		CMemorySerializer oldWriter;
		oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_ARMORER_LAST_STAND;
		EXPECT_THROW(oldWriter.oser & original, std::runtime_error);
		EXPECT_TRUE(oldWriter.extractBuffer().empty());
	}
}
