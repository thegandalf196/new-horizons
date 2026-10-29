/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsLivingInferno.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
TEST(NewHorizonsLivingInferno, FlightProjectionIsReversibleAdjacentAndBounded)
{
	for(int from = 0; from < GameConstants::BFIELD_SIZE; ++from)
		for(int to = 0; to < GameConstants::BFIELD_SIZE; ++to)
		{
			const BattleHex start(from), end(to);
			if(!start.isAvailable() || !end.isAvailable() || start == end)
				continue;
			const auto path = newHorizonsLivingInferno::flightPath(start, end);
			const auto reverse = newHorizonsLivingInferno::flightPath(end, start);
			ASSERT_EQ(path.size(), BattleHex::getDistance(start, end) + 1);
			ASSERT_EQ(path.size(), reverse.size());
			EXPECT_EQ(path.front(), start);
			EXPECT_EQ(path.back(), end);
			for(size_t i = 0; i < path.size(); ++i)
			{
				EXPECT_TRUE(path[i].isAvailable());
				EXPECT_EQ(path[i], reverse[path.size() - 1 - i]);
				if(i)
					EXPECT_EQ(BattleHex::getDistance(path[i - 1], path[i]), 1);
			}
		}
}
TEST(NewHorizonsLivingInferno, NoOpAndUnavailableFlightHaveNoTrail)
{
	EXPECT_TRUE(newHorizonsLivingInferno::flightPath(BattleHex(50), BattleHex(50)).empty());
	EXPECT_TRUE(newHorizonsLivingInferno::flightPath(BattleHex::INVALID, BattleHex(50)).empty());
	EXPECT_TRUE(newHorizonsLivingInferno::flightPath(BattleHex(0), BattleHex(50)).empty());
}
TEST(NewHorizonsLivingInferno, GroundTraversalUsesOnlyCommittedPacketPositions)
{
	newHorizonsLivingInferno::Trail trail;
	trail.damage = 10;
	trail.traverse(BattleHex(5, 5), BattleHexArray{BattleHex(6, 5), BattleHex(7, 5)}, false);
	trail.traverse(BattleHex(7, 5), BattleHexArray{BattleHex(7, 6)}, false);
	ASSERT_EQ(trail.visited.size(), 4);
	EXPECT_EQ(trail.visited.back(), BattleHex(7, 6));
	EXPECT_FALSE(trail.visited.contains(BattleHex(8, 6))); // Uncommitted suffix is absent.
}
TEST(NewHorizonsLivingInferno, FlyingTraversalIncludesIntermediatesFromEndpointOnlyPacket)
{
	newHorizonsLivingInferno::Trail trail;
	trail.damage = 10;
	trail.traverse(BattleHex(2, 2), BattleHexArray{BattleHex(12, 8)}, true);
	EXPECT_GT(trail.visited.size(), 2);
	EXPECT_EQ(trail.visited, newHorizonsLivingInferno::flightPath(BattleHex(2, 2), BattleHex(12, 8)));
}
TEST(NewHorizonsLivingInferno, RenderFlagCopiesAndCurrentWireRoundTrips)
{
	SpellCreatedObstacle wall;
	wall.pos = BattleHex(5, 5);
	wall.customSize = BattleHexArray{wall.pos, BattleHex(6, 5), BattleHex(7, 6)};
	EXPECT_EQ(wall.getRenderTiles(), BattleHexArray{wall.pos});
	wall.renderAtEachHex = true;
	EXPECT_EQ(wall.getRenderTiles(), wall.customSize);
	SpellCreatedObstacle copied = wall;
	EXPECT_TRUE(copied.renderAtEachHex);
	CMemorySerializer wire;
	wire.oser & wall;
	SpellCreatedObstacle restored;
	wire.iser & restored;
	EXPECT_TRUE(restored.renderAtEachHex);
	EXPECT_EQ(restored.getRenderTiles(), wall.customSize);
	wall.turnsRemaining = 1;
	wall.battleTurnPassed();
	EXPECT_EQ(wall.turnsRemaining, 0); // Existing round cleanup removes this logical obstacle.
}
TEST(NewHorizonsLivingInferno, OldObstacleWriterRejectsFlagBeforeItsPrefix)
{
	SpellCreatedObstacle wall;
	wall.renderAtEachHex = true;
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(wall.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
TEST(NewHorizonsLivingInferno, HistoricalObstacleLoadClearsReusedPresentationFlag)
{
	SpellCreatedObstacle old;
	old.pos = BattleHex(5, 5);
	old.customSize = BattleHexArray{old.pos};
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	old.serialize(wire.oser);
	SpellCreatedObstacle reused;
	reused.renderAtEachHex = true;
	reused.serialize(wire.iser);
	EXPECT_FALSE(reused.renderAtEachHex);
	EXPECT_EQ(reused.getRenderTiles(), BattleHexArray{old.pos});
}

TEST(NewHorizonsLivingInferno, CurrentObstaclePacketRetainsFlagAndOldWriterRejectsBeforePacketPrefix)
{
	SpellCreatedObstacle wall;
	wall.renderAtEachHex = true;
	wall.customSize = BattleHexArray{BattleHex(5, 5)};
	BattleObstaclesChanged packet;
	packet.battleID = BattleID(0);
	wall.toInfo(packet.change);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(packet.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer current;
	packet.serialize(current.oser);
	BattleObstaclesChanged restored;
	restored.serialize(current.iser);
	SpellCreatedObstacle decoded;
	decoded.fromInfo(restored.change);
	EXPECT_TRUE(decoded.renderAtEachHex);
	EXPECT_EQ(decoded.getRenderTiles(), wall.customSize);
}

TEST(NewHorizonsLivingInferno, MalformedPresentationPacketFlagRejects)
{
	for(const JsonNode flag : {JsonNode(), JsonNode("bad"), JsonNode(int32_t(1))})
	{
		ObstacleChanges change;
		change.data["obstacle"]["renderAtEachHex"] = flag;
		EXPECT_THROW(change.validatePresentationSerialization(true), std::runtime_error);
	}
}
