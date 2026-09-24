/*
 * MovementCostTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "GameStateTest.h"

#include "../../lib/GameConstants.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/CGObjectInstance.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/pathfinder/CPathfinder.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/pathfinder/TurnInfo.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"

namespace
{

constexpr ui8 FAVORABLE_WINDS_FLAG = 128;

class GameServerMock : public IGameServer
{
public:
	MOCK_METHOD(void, setState, (EServerState value), (override));
	MOCK_METHOD(EServerState, getState, (), (const, override));
	MOCK_METHOD(bool, isPlayerHost, (const PlayerColor & color), (const, override));
	MOCK_METHOD(bool, hasPlayerAt, (PlayerColor player, GameConnectionID connectionID), (const, override));
	MOCK_METHOD(bool, hasBothPlayersAtSameConnection, (PlayerColor left, PlayerColor right), (const, override));
	MOCK_METHOD(void, applyPack, (CPackForClient & pack), (override));
	MOCK_METHOD(void, sendPack, (CPackForClient & pack, GameConnectionID connectionID), (override));
};

class CoastVisitableObject : public CGObjectInstance
{
public:
	using CGObjectInstance::CGObjectInstance;
	bool isCoastVisitable() const override { return true; }
};

class BlockedCoastVisitableObject : public CoastVisitableObject
{
public:
	using CoastVisitableObject::CoastVisitableObject;

	void onHeroVisit(IGameEventCallback &, const CGHeroInstance *) const override
	{
		++visitCount;
	}

	mutable int visitCount = 0;
};

}

class MovementCostTest : public GameStateTest
{
};

class NewHorizonsMovementCostTest : public MovementCostTest
{
protected:
	void SetUp() override
	{
		MovementCostTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		MovementCostTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void addTravelBonus(CGHeroInstance * hero, BonusType type)
	{
		hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			BonusSource::SPELL_EFFECT, 0, BonusSourceID()));
	}

	void initializeGameHandler(testing::NiceMock<GameServerMock> & server)
	{
		ON_CALL(server, applyPack(testing::_)).WillByDefault([this](CPackForClient & pack)
		{
			gameState->apply(pack);
		});
	}
};

TEST_F(MovementCostTest, usesExplicitDestinationLayer)
{
	startTestGame();

	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	const auto * hero = heroes.front();

	const int3 sourcePosition = hero->visitablePos();
	const int3 destinationPosition = sourcePosition + int3(1, 0, 0);
	TerrainTile sourceTile = *gameState->getTile(sourcePosition);
	TerrainTile destinationTile = *gameState->getTile(destinationPosition);
	sourceTile.extTileFlags |= FAVORABLE_WINDS_FLAG;

	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	const int landCost = helper.getMovementCost(
		sourcePosition, destinationPosition, EPathfindingLayer::LAND, 1000, false, &sourceTile, &destinationTile);
	const int sailCost = helper.getMovementCost(
		sourcePosition, destinationPosition, EPathfindingLayer::SAIL, 1000, false, &sourceTile, &destinationTile);

	if(helper.getTurnInfo()->usesNewHorizonsMovement())
	{
		const bool road = sourceTile.hasRoad() && destinationTile.hasRoad();
		EXPECT_EQ(landCost, newHorizonsMovement::stepCost(false,
			helper.getTurnInfo()->hasNoTerrainPenalty(sourceTile.getTerrainID()),
			sourceTile.getTerrainID() == ETerrainId::SAND, road));
		EXPECT_EQ(sailCost, static_cast<int>(newHorizonsMovement::stepCost(false, true, false, false) * 2.0 / 3));
	}
	else
		EXPECT_EQ(sailCost, static_cast<int>(landCost * 2.0 / 3));
}

TEST_F(MovementCostTest, pathfinderAndExecutorUseSameSailCost)
{
	startTestGame();

	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	const auto * hero = heroes.front();
	const int3 sourcePosition = hero->visitablePos();
	const int3 embarkPosition = sourcePosition + int3(1, 0, 0);
	const int3 destinationPosition = sourcePosition + int3(2, 0, 0);

	auto & embarkTile = gameState->getMap().getTile(embarkPosition);
	auto & destinationTile = gameState->getMap().getTile(destinationPosition);
	embarkTile.terrainType = ETerrainId::WATER;
	destinationTile.terrainType = ETerrainId::WATER;

	testing::NiceMock<GameServerMock> server;
	ON_CALL(server, applyPack(testing::_)).WillByDefault([this](CPackForClient & pack)
	{
		gameState->apply(pack);
	});
	CGameHandler gameHandler(server, gameState);
	const int initialMovementPoints = hero->movementPointsRemaining();
	gameHandler.createBoat(embarkPosition, BoatId::CASTLE, hero->getOwner());
	ASSERT_FALSE(gameHandler.moveHero(
		hero->id,
		hero->convertFromVisitablePos(embarkPosition),
		EMovementMode::STANDARD,
		false,
		hero->getOwner(),
		EPathfindingLayer::AUTO));
	EXPECT_EQ(hero->visitablePos(), sourcePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovementPoints);

	ASSERT_TRUE(gameHandler.moveHero(
		hero->id,
		hero->convertFromVisitablePos(embarkPosition),
		EMovementMode::STANDARD,
		false,
		hero->getOwner(),
		EPathfindingLayer::SAIL));
	ASSERT_TRUE(hero->inBoat());
	gameHandler.setMovePoints(hero->id, initialMovementPoints);
	gameState->getMap().getTile(embarkPosition).extTileFlags |= FAVORABLE_WINDS_FLAG;

	CPathsInfo paths(gameState->getMapSize(), hero);
	auto config = std::make_shared<SingleHeroPathfinderConfig>(paths, *gameState, hero);
	CPathfinder pathfinder(*gameState, config);
	pathfinder.calculatePaths();

	const auto * destinationNode = paths.getNode(destinationPosition, EPathfindingLayer::SAIL);
	ASSERT_TRUE(destinationNode->reachable());
	ASSERT_EQ(destinationNode->turns, 0);
	ASSERT_EQ(destinationNode->action, EPathNodeAction::NORMAL);
	const int expectedMovementPoints = destinationNode->moveRemains;
	ASSERT_GT(expectedMovementPoints, 0);

	ASSERT_TRUE(gameHandler.moveHero(
		hero->id,
		hero->convertFromVisitablePos(destinationPosition),
		EMovementMode::STANDARD,
		false,
		hero->getOwner(),
		destinationNode->layer));
	EXPECT_EQ(hero->movementPointsRemaining(), expectedMovementPoints);
}

TEST_F(NewHorizonsMovementCostTest, helperAppliesSpecialTravelAcrossTerrainAndRoadCosts)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	const auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());

	const int3 sourcePosition = hero->visitablePos();
	const int3 destinationPosition = sourcePosition + int3(1, 0, 0);
	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	const std::array terrains = {ETerrainId::DIRT, ETerrainId::SAND, ETerrainId::GRASS, ETerrainId::SNOW,
		ETerrainId::SWAMP, ETerrainId::ROUGH, ETerrainId::SUBTERRANEAN, ETerrainId::LAVA, ETerrainId::WATER, ETerrainId::ROCK};

	for(const auto terrain : terrains)
		for(const bool road : {false, true})
		{
			TerrainTile sourceTile = *gameState->getTile(sourcePosition);
			TerrainTile destinationTile = *gameState->getTile(destinationPosition);
			sourceTile.terrainType = terrain;
			destinationTile.terrainType = terrain;
			sourceTile.roadType = road ? RoadId::DIRT_ROAD : RoadId::NO_ROAD;
			destinationTile.roadType = road ? RoadId::DIRT_ROAD : RoadId::NO_ROAD;

			const bool ordinaryWater = sourceTile.isWater();
			const int expectedAirCost = newHorizonsMovement::stepCost(false,
				ordinaryWater || helper.getTurnInfo()->hasNoTerrainPenalty(terrain),
				!ordinaryWater && terrain == ETerrainId::SAND,
				!ordinaryWater && road,
				true);
			EXPECT_EQ(helper.getMovementCost(sourcePosition, destinationPosition, EPathfindingLayer::AIR,
				1000, false, &sourceTile, &destinationTile), expectedAirCost)
				<< "terrain " << static_cast<int>(terrain) << ", road " << road;
			EXPECT_EQ(helper.getMovementCost(sourcePosition, destinationPosition, EPathfindingLayer::WATER,
				1000, false, &sourceTile, &destinationTile), 15)
				<< "water-layer terrain " << static_cast<int>(terrain) << ", road " << road;
		}
}

TEST_F(NewHorizonsMovementCostTest, ordinaryVisitableSourceDoesNotHideAnUnderlyingObstacle)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	const auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());

	const int3 sourcePosition = hero->visitablePos();
	const int3 destinationPosition = sourcePosition + int3(1, 0, 0);
	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	TerrainTile sourceTile = *gameState->getTile(sourcePosition);
	TerrainTile destinationTile = *gameState->getTile(destinationPosition);

	auto visitable = std::make_shared<CGObjectInstance>(gameState.get());
	visitable->id = ObjectInstanceID(static_cast<si32>(gameState->getMap().objects.size()));
	const auto visitableID = visitable->id;
	gameState->getMap().objects.push_back(visitable);
	sourceTile.blockingObjects.push_back(visitableID);
	sourceTile.visitableObjects.push_back(visitableID);

	const bool terrainAffinity = helper.getTurnInfo()->hasNoTerrainPenalty(sourceTile.getTerrainID());
	const bool desert = sourceTile.getTerrainID() == ETerrainId::SAND;
	const bool road = sourceTile.hasRoad() && destinationTile.hasRoad();
	const int ordinaryCost = helper.getMovementCost(sourcePosition, destinationPosition,
		EPathfindingLayer::LAND, 1000, false, &sourceTile, &destinationTile);
	EXPECT_EQ(ordinaryCost, newHorizonsMovement::stepCost(false, terrainAffinity, desert, road));

	auto obstacle = std::make_shared<CGObjectInstance>(gameState.get());
	obstacle->id = ObjectInstanceID(static_cast<si32>(gameState->getMap().objects.size()));
	const auto obstacleID = obstacle->id;
	gameState->getMap().objects.push_back(obstacle);
	sourceTile.blockingObjects.push_back(obstacleID);
	const int obstacleSourceCost = helper.getMovementCost(sourcePosition, destinationPosition,
		EPathfindingLayer::LAND, 1000, false, &sourceTile, &destinationTile);
	EXPECT_EQ(obstacleSourceCost, newHorizonsMovement::stepCost(false, terrainAffinity, desert, road, true));
}

TEST_F(NewHorizonsMovementCostTest, pathfinderAndExecutorUseSameWaterWalkCost)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());
	ASSERT_FALSE(hero->inBoat());
	addTravelBonus(hero, BonusType::WATER_WALKING);

	const int3 sourcePosition = hero->visitablePos();
	const int3 waterPosition = sourcePosition + int3(1, 0, 0);
	gameState->getMap().getTile(waterPosition).terrainType = ETerrainId::WATER;

	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	const int expectedStepCost = helper.getMovementCost(sourcePosition, waterPosition,
		EPathfindingLayer::WATER, hero->movementPointsRemaining());
	ASSERT_EQ(expectedStepCost, 15);
	const int initialMovementPoints = hero->movementPointsRemaining();
	CPathsInfo paths(gameState->getMapSize(), hero);
	auto config = std::make_shared<SingleHeroPathfinderConfig>(paths, *gameState, hero);
	CPathfinder pathfinder(*gameState, config);
	pathfinder.calculatePaths();

	const auto * destinationNode = paths.getNode(waterPosition, EPathfindingLayer::WATER);
	ASSERT_TRUE(destinationNode->reachable());
	ASSERT_EQ(destinationNode->turns, 0);
	ASSERT_EQ(destinationNode->moveRemains, initialMovementPoints - expectedStepCost);

	testing::NiceMock<GameServerMock> server;
	initializeGameHandler(server);
	CGameHandler gameHandler(server, gameState);
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(waterPosition),
		EMovementMode::STANDARD, true, hero->getOwner(), destinationNode->layer));
	EXPECT_EQ(hero->visitablePos(), waterPosition);
	EXPECT_EQ(hero->movementPointsRemaining(), destinationNode->moveRemains);
}

TEST_F(NewHorizonsMovementCostTest, waterWalkCanEndOnLandAtTheCanonicalLandingCost)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());
	addTravelBonus(hero, BonusType::WATER_WALKING);

	const int3 start = hero->visitablePos();
	const int3 waterPosition = start + int3(1, 0, 0);
	const int3 landPosition = start + int3(2, 0, 0);
	gameState->getMap().getTile(waterPosition).terrainType = ETerrainId::WATER;
	gameState->getMap().getTile(landPosition).terrainType = ETerrainId::GRASS;

	testing::NiceMock<GameServerMock> server;
	initializeGameHandler(server);
	CGameHandler gameHandler(server, gameState);
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(waterPosition),
		EMovementMode::STANDARD, true, hero->getOwner(), EPathfindingLayer::WATER));
	ASSERT_EQ(hero->visitablePos(), waterPosition);

	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	const int landingCost = helper.getMovementCost(waterPosition, landPosition,
		EPathfindingLayer::LAND, hero->movementPointsRemaining());
	ASSERT_EQ(landingCost, 15);
	const int beforeLanding = hero->movementPointsRemaining();
	CPathsInfo paths(gameState->getMapSize(), hero);
	auto config = std::make_shared<SingleHeroPathfinderConfig>(paths, *gameState, hero);
	CPathfinder pathfinder(*gameState, config);
	pathfinder.calculatePaths();

	const auto * destinationNode = paths.getNode(landPosition, EPathfindingLayer::LAND);
	ASSERT_TRUE(destinationNode->reachable());
	ASSERT_EQ(destinationNode->turns, 0);
	ASSERT_EQ(destinationNode->moveRemains, beforeLanding - landingCost);
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(landPosition),
		EMovementMode::STANDARD, false, hero->getOwner(), destinationNode->layer));
	EXPECT_EQ(hero->visitablePos(), landPosition);
	EXPECT_EQ(hero->movementPointsRemaining(), destinationNode->moveRemains);
}

TEST_F(NewHorizonsMovementCostTest, flyCanEndOnLandAfterCrossingWaterAtTheCanonicalLandingCost)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());
	addTravelBonus(hero, BonusType::FLYING_MOVEMENT);

	const int3 start = hero->visitablePos();
	const int3 waterPosition = start + int3(1, 0, 0);
	const int3 landPosition = start + int3(2, 0, 0);
	gameState->getMap().getTile(waterPosition).terrainType = ETerrainId::WATER;
	gameState->getMap().getTile(landPosition).terrainType = ETerrainId::GRASS;

	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	const int flightCost = helper.getMovementCost(start, waterPosition,
		EPathfindingLayer::AIR, hero->movementPointsRemaining());
	const int initialMovementPoints = hero->movementPointsRemaining();
	CPathsInfo paths(gameState->getMapSize(), hero);
	auto config = std::make_shared<SingleHeroPathfinderConfig>(paths, *gameState, hero);
	CPathfinder pathfinder(*gameState, config);
	pathfinder.calculatePaths();

	const auto * waterNode = paths.getNode(waterPosition, EPathfindingLayer::AIR);
	ASSERT_TRUE(waterNode->reachable());
	ASSERT_EQ(waterNode->turns, 0);
	ASSERT_EQ(waterNode->moveRemains, initialMovementPoints - flightCost);

	testing::NiceMock<GameServerMock> server;
	initializeGameHandler(server);
	CGameHandler gameHandler(server, gameState);
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(waterPosition),
		EMovementMode::STANDARD, false, hero->getOwner(), waterNode->layer));
	ASSERT_EQ(hero->visitablePos(), waterPosition);
	EXPECT_EQ(hero->movementPointsRemaining(), waterNode->moveRemains);

	CPathfinderHelper landingHelper(*gameState, hero, PathfinderOptions(*gameState));
	const int landingCost = landingHelper.getMovementCost(waterPosition, landPosition,
		EPathfindingLayer::LAND, hero->movementPointsRemaining());
	ASSERT_EQ(landingCost, 15);
	const int beforeLanding = hero->movementPointsRemaining();
	CPathsInfo landingPaths(gameState->getMapSize(), hero);
	auto landingConfig = std::make_shared<SingleHeroPathfinderConfig>(landingPaths, *gameState, hero);
	CPathfinder landingPathfinder(*gameState, landingConfig);
	landingPathfinder.calculatePaths();

	const auto * landNode = landingPaths.getNode(landPosition, EPathfindingLayer::LAND);
	ASSERT_TRUE(landNode->reachable());
	ASSERT_EQ(landNode->turns, 0);
	ASSERT_EQ(landNode->moveRemains, beforeLanding - landingCost);
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(landPosition),
		EMovementMode::STANDARD, false, hero->getOwner(), landNode->layer));
	EXPECT_EQ(hero->visitablePos(), landPosition);
	EXPECT_EQ(hero->movementPointsRemaining(), landNode->moveRemains);
}

TEST_F(NewHorizonsMovementCostTest, rejectsForgedLandOrSailLayersForWaterAndObstacleTiles)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());

	const int3 sourcePosition = hero->visitablePos();
	const int3 waterPosition = sourcePosition + int3(1, 0, 0);
	const int3 obstaclePosition = sourcePosition + int3(0, 1, 0);
	gameState->getMap().getTile(waterPosition).terrainType = ETerrainId::WATER;
	gameState->getMap().getTile(obstaclePosition).terrainType = ETerrainId::GRASS;
	const auto opposingHeroes = gameState->getPlayerState(PlayerColor(1))->getHeroes();
	ASSERT_FALSE(opposingHeroes.empty());
	gameState->getMap().getTile(obstaclePosition).blockingObjects.push_back(opposingHeroes.front()->id);

	testing::NiceMock<GameServerMock> server;
	initializeGameHandler(server);
	CGameHandler gameHandler(server, gameState);
	const int initialMovementPoints = hero->movementPointsRemaining();
	const auto attempt = [&](const int3 & destination, EPathfindingLayer layer)
	{
		return gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(destination),
			EMovementMode::STANDARD, false, hero->getOwner(), layer);
	};

	EXPECT_FALSE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(waterPosition),
		EMovementMode::STANDARD, true, hero->getOwner(), EPathfindingLayer::WATER));
	EXPECT_EQ(hero->visitablePos(), sourcePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovementPoints);
	addTravelBonus(hero, BonusType::WATER_WALKING);
	EXPECT_FALSE(attempt(waterPosition, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), sourcePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovementPoints);
	EXPECT_FALSE(attempt(waterPosition, EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->visitablePos(), sourcePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovementPoints);
	EXPECT_FALSE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(waterPosition),
		EMovementMode::STANDARD, true, hero->getOwner(), EPathfindingLayer::AIR));
	EXPECT_EQ(hero->visitablePos(), sourcePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovementPoints);
	EXPECT_FALSE(attempt(obstaclePosition, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), sourcePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovementPoints);

	// Having Fly must not allow an inexpensive LAND request to bypass visits
	// and guards using the transit flag. Ordinary teleport transit is separate.
	addTravelBonus(hero, BonusType::FLYING_MOVEMENT);
	const int3 landPosition = sourcePosition + int3(-1, 0, 0);
	gameState->getMap().getTile(landPosition).terrainType = ETerrainId::GRASS;
	EXPECT_FALSE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(landPosition),
		EMovementMode::STANDARD, true, hero->getOwner(), EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), sourcePosition);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovementPoints);
}

TEST_F(NewHorizonsMovementCostTest, coastVisitableWaterObjectDoesNotGrantSailingToHeroOnFoot)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	auto * hero = heroes.front();
	ASSERT_FALSE(hero->inBoat());
	const int3 source = hero->visitablePos();
	const int3 destination = source + int3(1, 0, 0);
	auto & tile = gameState->getMap().getTile(destination);
	tile.terrainType = ETerrainId::WATER;

	auto object = std::make_shared<CoastVisitableObject>(gameState.get());
	object->id = ObjectInstanceID(static_cast<si32>(gameState->getMap().objects.size()));
	object->ID = Obj::FLOTSAM;
	gameState->getMap().objects.push_back(object);
	tile.visitableObjects.push_back(object->id);

	testing::NiceMock<GameServerMock> server;
	initializeGameHandler(server);
	CGameHandler gameHandler(server, gameState);
	const int initialMovement = hero->movementPointsRemaining();
	EXPECT_FALSE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(destination),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	EXPECT_EQ(hero->visitablePos(), source);
	EXPECT_EQ(hero->movementPointsRemaining(), initialMovement);
	EXPECT_FALSE(hero->inBoat());
}

TEST_F(NewHorizonsMovementCostTest, blockedCoastVisitableWaterObjectIsVisitedFromShoreAtWalkingCost)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());
	ASSERT_FALSE(hero->inBoat());

	const int3 source = hero->visitablePos();
	const int3 destination = source + int3(1, 0, 0);
	auto & sourceTile = gameState->getMap().getTile(source);
	auto & destinationTile = gameState->getMap().getTile(destination);

	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	const std::array landTerrains = {ETerrainId::SAND, ETerrainId::DIRT, ETerrainId::GRASS, ETerrainId::SNOW,
		ETerrainId::SWAMP, ETerrainId::ROUGH, ETerrainId::SUBTERRANEAN, ETerrainId::LAVA};
	const auto nonNativeTerrain = std::find_if(landTerrains.begin(), landTerrains.end(), [&](ETerrainId terrain)
	{
		return !helper.getTurnInfo()->hasNoTerrainPenalty(terrain)
			&& newHorizonsMovement::stepCost(false, false, terrain == ETerrainId::SAND, false) > 10;
	});
	ASSERT_NE(nonNativeTerrain, landTerrains.end());

	sourceTile.terrainType = *nonNativeTerrain;
	sourceTile.extTileFlags &= static_cast<ui8>(~FAVORABLE_WINDS_FLAG);
	destinationTile.terrainType = ETerrainId::WATER;

	auto object = std::make_shared<BlockedCoastVisitableObject>(gameState.get());
	object->id = ObjectInstanceID(static_cast<si32>(gameState->getMap().objects.size()));
	object->ID = Obj::SHIPWRECK;
	object->subID = MapObjectSubID(0);
	// Supply visit-direction geometry for the pathfinder; no artwork is
	// rendered by this synthetic blocking-visit fixture.
	object->appearance = hero->appearance;
	object->pos = destination + object->getVisitableOffset();
	object->blockVisit = true;
	const auto objectID = object->id;
	gameState->getMap().objects.push_back(object);
	destinationTile.blockingObjects.push_back(objectID);
	destinationTile.visitableObjects.push_back(objectID);
	ASSERT_TRUE(object->isBlockedVisitable());
	ASSERT_TRUE(object->isCoastVisitable());

	const int initialMovement = hero->movementPointsRemaining();
	const bool terrainAffinity = helper.getTurnInfo()->hasNoTerrainPenalty(*nonNativeTerrain);
	const int expectedWalkingCost = newHorizonsMovement::stepCost(false, terrainAffinity,
		*nonNativeTerrain == ETerrainId::SAND, sourceTile.hasRoad() && destinationTile.hasRoad());
	ASSERT_GT(expectedWalkingCost, 10);
	EXPECT_EQ(helper.getMovementCost(source, destination, EPathfindingLayer::LAND, initialMovement), expectedWalkingCost);
	EXPECT_EQ(helper.getMovementCost(source, destination, EPathfindingLayer::SAIL, initialMovement), expectedWalkingCost);

	CPathsInfo paths(gameState->getMapSize(), hero);
	auto config = std::make_shared<SingleHeroPathfinderConfig>(paths, *gameState, hero);
	CPathfinder pathfinder(*gameState, config);
	pathfinder.calculatePaths();

	const auto * destinationNode = paths.getNode(destination, EPathfindingLayer::SAIL);
	ASSERT_TRUE(destinationNode->reachable());
	ASSERT_EQ(destinationNode->action, EPathNodeAction::BLOCKING_VISIT);
	ASSERT_EQ(destinationNode->moveRemains, initialMovement - expectedWalkingCost);

	testing::NiceMock<GameServerMock> server;
	initializeGameHandler(server);
	CGameHandler gameHandler(server, gameState);
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(destination),
		EMovementMode::STANDARD, false, hero->getOwner(), destinationNode->layer));
	EXPECT_EQ(object->visitCount, 1);
	EXPECT_EQ(hero->visitablePos(), source);
	EXPECT_EQ(hero->movementPointsRemaining(), destinationNode->moveRemains);
	EXPECT_FALSE(hero->inBoat());
}

TEST_F(NewHorizonsMovementCostTest, waterLayerVehicleKeepsOrdinaryWaterStepCost)
{
	startTestGame();
	const auto heroes = gameState->getPlayerState(PlayerColor(0))->getHeroes();
	ASSERT_FALSE(heroes.empty());
	auto * hero = heroes.front();
	ASSERT_TRUE(hero->usesNewHorizonsMovement());

	const int3 sourcePosition = hero->visitablePos();
	const int3 embarkPosition = sourcePosition + int3(1, 0, 0);
	const int3 destinationPosition = sourcePosition + int3(2, 0, 0);
	gameState->getMap().getTile(embarkPosition).terrainType = ETerrainId::WATER;
	gameState->getMap().getTile(destinationPosition).terrainType = ETerrainId::WATER;

	testing::NiceMock<GameServerMock> server;
	initializeGameHandler(server);
	CGameHandler gameHandler(server, gameState);
	gameHandler.createBoat(embarkPosition, BoatId::CASTLE, hero->getOwner());
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(embarkPosition),
		EMovementMode::STANDARD, false, hero->getOwner(), EPathfindingLayer::SAIL));
	ASSERT_TRUE(hero->inBoat());
	hero->getBoat()->layer = EPathfindingLayer::WATER;
	gameHandler.setMovePoints(hero->id, 1000);

	CPathfinderHelper helper(*gameState, hero, PathfinderOptions(*gameState));
	EXPECT_EQ(helper.getMovementCost(embarkPosition, destinationPosition,
		EPathfindingLayer::WATER, hero->movementPointsRemaining()), 10);
	CPathsInfo paths(gameState->getMapSize(), hero);
	auto config = std::make_shared<SingleHeroPathfinderConfig>(paths, *gameState, hero);
	CPathfinder pathfinder(*gameState, config);
	pathfinder.calculatePaths();

	const auto * destinationNode = paths.getNode(destinationPosition, EPathfindingLayer::WATER);
	ASSERT_TRUE(destinationNode->reachable());
	ASSERT_EQ(destinationNode->turns, 0);
	EXPECT_EQ(destinationNode->moveRemains, 990);
	ASSERT_TRUE(gameHandler.moveHero(hero->id, hero->convertFromVisitablePos(destinationPosition),
		EMovementMode::STANDARD, true, hero->getOwner(), destinationNode->layer));
	EXPECT_EQ(hero->visitablePos(), destinationPosition);
	EXPECT_EQ(hero->movementPointsRemaining(), destinationNode->moveRemains);
}
