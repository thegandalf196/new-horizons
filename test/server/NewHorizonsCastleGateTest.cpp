/*
 * NewHorizonsCastleGateTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file in main folder
 */
#include "StdInc.h"

#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../mock/TinyMapGameTest.h"

#include <algorithm>
#include <optional>

namespace
{

class CastleGateRecordingServer final : public IGameServer
{
public:
	explicit CastleGateRecordingServer(std::shared_ptr<CGameState> gameState)
		: gameState(std::move(gameState))
	{}

	void setState(EServerState value) override { state = value; }
	EServerState getState() const override { return state; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor, GameConnectionID) const override { return true; }
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return true; }

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * gateState = dynamic_cast<const SetNewHorizonsCastleGateState *>(&pack))
		{
			lastGateState = *gateState;
			++gateStatePackCount;
		}
		gameState->apply(pack);
	}

	void sendPack(CPackForClient &, GameConnectionID) override {}

	std::optional<SetNewHorizonsCastleGateState> lastGateState;
	int gateStatePackCount = 0;

private:
	EServerState state = EServerState::GAMEPLAY;
	std::shared_ptr<CGameState> gameState;
};

class NewHorizonsCastleGateTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void startGateMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({4, 4, 0}, FactionID::INFERNO, PlayerColor(0))
			.town({14, 4, 0}, FactionID::INFERNO, PlayerColor(0))
			.town({24, 4, 0}, FactionID::INFERNO, PlayerColor(0))
			.town({4, 14, 0}, FactionID::INFERNO, PlayerColor(1))
			.town({14, 14, 0}, FactionID::CASTLE, PlayerColor(0))
			.town({24, 14, 0}, FactionID::INFERNO, PlayerColor(0))
			.hero({8, 8, 0}, HeroTypeID(0), PlayerColor(0))
			.hero({19, 19, 0}, HeroTypeID(1), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};

BuildingID castleGateBuilding(const CGTownInstance & town)
{
	const auto it = std::find_if(town.getTown()->buildings.begin(), town.getTown()->buildings.end(), [](const auto & entry)
	{
		return entry.second->subId == BuildingSubID::CASTLE_GATE;
	});
	return it == town.getTown()->buildings.end() ? BuildingID::NONE : it->first;
}

} // namespace

TEST_F(NewHorizonsCastleGateTest, OwnedInfernoGatesTeleportOncePerDayAndSpendAllMovement)
{
	startGateMap();
	auto * source = expectAt<CGTownInstance>({4, 4, 0});
	auto * destination = expectAt<CGTownInstance>({14, 4, 0});
	auto * occupiedDestination = expectAt<CGTownInstance>({24, 4, 0});
	auto * rivalInferno = expectAt<CGTownInstance>({4, 14, 0});
	auto * nonInferno = expectAt<CGTownInstance>({14, 14, 0});
	auto * ungatedInferno = expectAt<CGTownInstance>({24, 14, 0});
	auto * hero = findHeroAt({8, 8, 0});
	auto * occupant = findHeroAt({19, 19, 0});
	ASSERT_NE(source, nullptr);
	ASSERT_NE(destination, nullptr);
	ASSERT_NE(occupiedDestination, nullptr);
	ASSERT_NE(rivalInferno, nullptr);
	ASSERT_NE(nonInferno, nullptr);
	ASSERT_NE(ungatedInferno, nullptr);
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(occupant, nullptr);

	CastleGateRecordingServer server(gameState());
	CGameHandler handler(server, gameState());
	auto buildGate = [&handler](CGTownInstance * town)
	{
		const auto gate = castleGateBuilding(*town);
		if(gate == BuildingID::NONE)
			return false;
		return handler.buildStructure(town->id, gate, true);
	};
	ASSERT_TRUE(buildGate(source));
	ASSERT_TRUE(buildGate(destination));
	ASSERT_TRUE(buildGate(occupiedDestination));
	ASSERT_TRUE(buildGate(rivalInferno));
	EXPECT_FALSE(ungatedInferno->hasBuilt(BuildingSubID::CASTLE_GATE));
	EXPECT_FALSE(nonInferno->hasBuilt(BuildingSubID::CASTLE_GATE));

	const int3 sourceHeroPosition = hero->convertFromVisitablePos(source->visitablePos());
	ASSERT_TRUE(handler.moveHero(hero->id, sourceHeroPosition, EMovementMode::TOWN_PORTAL));
	ASSERT_EQ(hero->getVisitedTown(), source);
	ASSERT_EQ(hero->visitablePos(), source->visitablePos());
	handler.setMovePoints(hero->id, 700);
	ASSERT_GT(hero->movementPointsRemaining(), 0);
	const int initialDay = gameState()->getCalendar().getCurrentDay();

	// Destination ownership, town faction, gate presence, and occupancy all fail
	// before an accepted-use marker is recorded.
	EXPECT_FALSE(handler.teleportHero(hero->id, rivalInferno->id, 1));
	EXPECT_FALSE(handler.teleportHero(hero->id, nonInferno->id, 1));
	EXPECT_FALSE(handler.teleportHero(hero->id, ungatedInferno->id, 1));
	occupiedDestination->setVisitingHero(occupant);
	EXPECT_FALSE(handler.teleportHero(hero->id, occupiedDestination->id, 1));
	EXPECT_EQ(hero->getVisitedTown(), source);
	EXPECT_EQ(hero->movementPointsRemaining(), 700);
	EXPECT_FALSE(hero->hasUsedNewHorizonsCastleGateToday(initialDay));
	EXPECT_EQ(server.gateStatePackCount, 0);
	EXPECT_FALSE(handler.teleportHero(hero->id, source->id, 1)); // A gate must connect distinct towns.
	EXPECT_EQ(hero->getVisitedTown(), source);
	EXPECT_EQ(hero->movementPointsRemaining(), 700);
	EXPECT_FALSE(hero->hasUsedNewHorizonsCastleGateToday(initialDay));
	EXPECT_EQ(server.gateStatePackCount, 0);

	// A source outside the hero's owned pair is not controlled, even when a
	// fixture or ally state places the hero there.
	source->setVisitingHero(nullptr);
	rivalInferno->setVisitingHero(hero);
	EXPECT_FALSE(handler.teleportHero(hero->id, destination->id, 1));
	EXPECT_EQ(hero->getVisitedTown(), rivalInferno);
	EXPECT_EQ(hero->movementPointsRemaining(), 700);
	EXPECT_FALSE(hero->hasUsedNewHorizonsCastleGateToday(initialDay));
	EXPECT_EQ(server.gateStatePackCount, 0);

	rivalInferno->setVisitingHero(nullptr);
	nonInferno->setVisitingHero(hero);
	EXPECT_FALSE(handler.teleportHero(hero->id, destination->id, 1));
	EXPECT_EQ(hero->getVisitedTown(), nonInferno);
	EXPECT_EQ(hero->movementPointsRemaining(), 700);
	EXPECT_FALSE(hero->hasUsedNewHorizonsCastleGateToday(initialDay));
	EXPECT_EQ(server.gateStatePackCount, 0);

	nonInferno->setVisitingHero(nullptr);
	ungatedInferno->setVisitingHero(hero);
	EXPECT_FALSE(handler.teleportHero(hero->id, destination->id, 1));
	EXPECT_EQ(hero->getVisitedTown(), ungatedInferno);
	EXPECT_EQ(hero->movementPointsRemaining(), 700);
	EXPECT_FALSE(hero->hasUsedNewHorizonsCastleGateToday(initialDay));
	EXPECT_EQ(server.gateStatePackCount, 0);

	ungatedInferno->setVisitingHero(nullptr);
	// The negative source cases only rearranged visitor ownership; the hero
	// stayed physically at the source town's visitable position.
	ASSERT_EQ(hero->pos, sourceHeroPosition);
	source->setVisitingHero(hero);
	ASSERT_EQ(hero->getVisitedTown(), source);
	ASSERT_EQ(hero->visitablePos(), source->visitablePos());
	handler.setMovePoints(hero->id, 700);
	ASSERT_TRUE(handler.teleportHero(hero->id, destination->id, 1));
	EXPECT_EQ(hero->getVisitedTown(), destination);
	EXPECT_EQ(hero->visitablePos(), destination->visitablePos());
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_TRUE(hero->hasUsedNewHorizonsCastleGateToday(initialDay));
	ASSERT_TRUE(server.lastGateState.has_value());
	EXPECT_EQ(server.lastGateState->hid, hero->id);
	EXPECT_EQ(server.lastGateState->lastUseDay, initialDay);
	EXPECT_EQ(server.gateStatePackCount, 1);

	const int3 destinationPosition = hero->visitablePos();
	EXPECT_FALSE(handler.teleportHero(hero->id, source->id, 1));
	EXPECT_EQ(hero->visitablePos(), destinationPosition);
	EXPECT_EQ(hero->getVisitedTown(), destination);
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_EQ(server.gateStatePackCount, 1);

	// The regular authoritative NewTurn path advances the daily marker and
	// restores Movement; the same pair can then be used again.
	handler.onNewTurn();
	const int nextDay = gameState()->getCalendar().getCurrentDay();
	ASSERT_GT(nextDay, initialDay);
	EXPECT_FALSE(hero->hasUsedNewHorizonsCastleGateToday(nextDay));
	ASSERT_GT(hero->movementPointsRemaining(), 0);
	ASSERT_TRUE(handler.teleportHero(hero->id, source->id, 1));
	EXPECT_EQ(hero->getVisitedTown(), source);
	EXPECT_EQ(hero->visitablePos(), source->visitablePos());
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_TRUE(hero->hasUsedNewHorizonsCastleGateToday(nextDay));
	ASSERT_TRUE(server.lastGateState.has_value());
	EXPECT_EQ(server.lastGateState->lastUseDay, nextDay);
	EXPECT_EQ(server.gateStatePackCount, 2);
}
