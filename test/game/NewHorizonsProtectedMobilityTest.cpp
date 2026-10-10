/*
 * NewHorizonsProtectedMobilityTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../SpellPointTestUtils.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/StartInfo.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/pathfinder/NewHorizonsProtectedMobility.h"
#include "../../lib/pathfinder/PathfinderCache.h"
#include "../../lib/pathfinder/CGPathNode.h"
#include "../../lib/pathfinder/PathfinderOptions.h"
#include "../../lib/pathfinder/TurnInfo.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/adventure/DimensionDoorEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/filesystem/CMemoryBuffer.h"
#include "../../lib/filesystem/CZipLoader.h"
#include "../../lib/mapping/MapFormatJson.h"
#include "../../server/CGameHandler.h"

namespace
{
class NewHorizonsProtectedMobilityTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_TRUE(rules["protectedAdventureBarriers"].isBool());
		ASSERT_TRUE(rules["protectedAdventureBarriers"].Bool());
		if(!feature)
			rules.Struct().erase("protectedAdventureBarriers");
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		auto heroRules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!feature)
			heroRules.Struct().erase("defaultCreatureLineReplacements");
		heroRules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroRules);
	}
	void start(bool active = true)
	{
		feature = active;
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Protected mobility").playerActive(PlayerColor(0))
			.hero({10, 10, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
		startWithMap(std::move(builder));
		hero = findFirst<CGHeroInstance>();
		ASSERT_NE(hero, nullptr);
		origin = hero->visitablePos();
		for(int x = origin.x - 2; x <= origin.x + 8; ++x)
			for(int y = origin.y - 2; y <= origin.y + 8; ++y)
				map()->getTile({x, y, 0}).terrainType = ETerrainId::GRASS;
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(hero, 100);
		hero->setMovementPoints(2000);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PlayerColor(0));
		handler = std::make_unique<CGameHandler>(*server, gameState());
	}
	bool cast(SpellID spell, int3 destination = int3())
	{
		hero->addSpellToSpellbook(spell);
		AdventureSpellCastParameters parameters;
		parameters.caster = hero;
		parameters.pos = destination;
		return spell.toSpell()->adventureCast(dynamic_cast<SpellCastEnvironment *>(handler->spellcastEnvironment()), parameters);
	}
	bool move(int3 destination, EPathfindingLayer layer)
	{
		return handler->moveHero(hero->id, hero->convertFromVisitablePos(destination),
			EMovementMode::STANDARD, false, hero->getOwner(), layer);
	}
	void fly()
	{
		const auto before = hero->getManaAvailable();
		ASSERT_TRUE(cast(SpellID(SpellID::decode("core:fly"))));
		EXPECT_EQ(hero->getManaAvailable(), before - 60);
		ASSERT_TRUE(hero->getTurnInfo(0)->hasFlyingMovement());
		EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
	}
	bool feature = true;
	CGHeroInstance * hero = nullptr;
	int3 origin;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
};
}

TEST(NewHorizonsProtectedMobilityRulesTest, ClosedCornersAreSymmetricAndDoNotInferTerrain)
{
	using namespace newHorizonsProtectedMobility;
	const std::vector<int3> tiles{{3, 2, 0}};
	EXPECT_FALSE(segmentClear(tiles, {2, 2, 0}, {3, 3, 0}));
	EXPECT_FALSE(segmentClear(tiles, {3, 3, 0}, {2, 2, 0}));
	EXPECT_TRUE(segmentClear(tiles, {2, 2, 0}, {2, 3, 0}));
	EXPECT_TRUE(segmentClear({}, {2, 2, 0}, {3, 3, 0}));
	EXPECT_FALSE(segmentClear({}, {2, 2, 0}, {3, 3, 1}));
}

TEST(NewHorizonsProtectedMobilityRulesTest, AxialAndShallowSupercoverIncludeEndpointsAndCrossedCells)
{
	using namespace newHorizonsProtectedMobility;
	EXPECT_FALSE(segmentClear({{4, 2, 0}}, {2, 2, 0}, {6, 2, 0}));
	EXPECT_FALSE(segmentClear({{6, 2, 0}}, {2, 2, 0}, {6, 2, 0}));
	EXPECT_FALSE(segmentClear({{2, 2, 0}}, {2, 2, 0}, {6, 2, 0}));
	EXPECT_FALSE(segmentClear({{4, 3, 0}}, {2, 2, 0}, {6, 4, 0}));
	EXPECT_TRUE(segmentClear({{4, 4, 0}}, {2, 2, 0}, {6, 4, 0}));
}

TEST(NewHorizonsProtectedMobilityRulesTest, AuthoredTriplesCanonicalizeAndRejectMalformedOrDuplicates)
{
	using namespace newHorizonsProtectedMobility;
	const std::vector<int3> expected{{2, 2, 0}, {3, 2, 0}};
	auto authored = writeTiles({{3, 2, 0}, {2, 2, 0}});
	EXPECT_EQ(readTiles(authored, 36, 36, 1), expected);
	authored.Vector().push_back(authored.Vector().front());
	EXPECT_THROW(readTiles(authored, 36, 36, 1), std::runtime_error);
	authored = writeTiles(expected);
	authored.Vector()[0].Vector()[0].Float() = 2.5;
	EXPECT_THROW(readTiles(authored, 36, 36, 1), std::runtime_error);
	authored = writeTiles({{36, 2, 0}});
	EXPECT_THROW(readTiles(authored, 36, 36, 1), std::runtime_error);
	EXPECT_THROW(readTiles(JsonNode(), 36, 36, 1), std::runtime_error);
}

TEST(NewHorizonsProtectedMobilityRulesTest, AnyCapturedPresenceRejectsOlderFormatsAndAbsentIsLegacy)
{
	using namespace newHorizonsProtectedMobility;
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_TRUE(enabled(rules));
	EXPECT_THROW(validateRulesSerialization(rules, false), std::runtime_error);
	rules["protectedAdventureBarriers"].Bool() = false;
	EXPECT_FALSE(enabled(rules));
	EXPECT_THROW(validateRulesSerialization(rules, false), std::runtime_error);
	rules.Struct().erase("protectedAdventureBarriers");
	EXPECT_NO_THROW(validateRulesSerialization(rules, false));
	EXPECT_FALSE(enabled(rules));
	rules["protectedAdventureBarriers"].String() = "true";
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}

TEST_F(NewHorizonsProtectedMobilityTest, PaidFlyDoesNotLiftOrdinaryWalkingIntoBarrierGating)
{
	start();
	fly();
	const auto destination = origin + int3(1, 0, 0);
	map()->setProtectedAdventureMobilityTiles({destination});
	ASSERT_TRUE(move(destination, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), destination);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
}

TEST_F(NewHorizonsProtectedMobilityTest, ActualAirStepRejectsClosedCornerWithoutCostOrStateChange)
{
	start();
	fly();
	map()->setProtectedAdventureMobilityTiles({origin + int3(1, 0, 0)});
	const auto before = hero->movementPointsRemaining();
	EXPECT_FALSE(move(origin + int3(1, 1, 0), EPathfindingLayer::AIR));
	EXPECT_EQ(hero->visitablePos(), origin);
	EXPECT_EQ(hero->movementPointsRemaining(), before);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
}

TEST_F(NewHorizonsProtectedMobilityTest, AirOnClearTerrainCannotMasqueradeAsUnprotectedLandLanding)
{
	start();
	fly();
	const auto airborne = origin + int3(1, 0, 0);
	ASSERT_TRUE(move(airborne, EPathfindingLayer::AIR));
	ASSERT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::AIR);
	const auto destination = airborne + int3(1, 1, 0);
	map()->setProtectedAdventureMobilityTiles({airborne + int3(1, 0, 0)});
	const auto before = hero->movementPointsRemaining();
	EXPECT_FALSE(move(destination, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), airborne);
	EXPECT_EQ(hero->movementPointsRemaining(), before);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::AIR);
	map()->setProtectedAdventureMobilityTiles({});
	ASSERT_TRUE(move(destination, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
}

TEST_F(NewHorizonsProtectedMobilityTest, SharedPathStartsFromAcceptedAirAndCannotOfferBlockedLanding)
{
	start();
	fly();
	ASSERT_TRUE(move(origin + int3(1, 0, 0), EPathfindingLayer::AIR));
	const auto start = hero->visitablePos();
	std::vector<int3> surrounding;
	for(int x = -1; x <= 1; ++x)
		for(int y = -1; y <= 1; ++y)
			if(x || y)
				surrounding.push_back(start + int3(x, y, 0));
	map()->setProtectedAdventureMobilityTiles(std::move(surrounding));
	PathfinderCache cache(gameState().get(), PathfinderOptions(*gameState()));
	const auto paths = cache.getPathsInfo(hero);
	const auto * initial = paths->getNode(start, EPathfindingLayer::AIR);
	ASSERT_NE(initial, nullptr);
	EXPECT_EQ(initial->moveRemains, hero->movementPointsRemaining());
	const auto * destination = paths->getNode(start + int3(1, 0, 0), EPathfindingLayer::LAND);
	ASSERT_NE(destination, nullptr);
	EXPECT_FALSE(destination->reachable());
}

TEST_F(NewHorizonsProtectedMobilityTest, PaidDimensionDoorBlockedSegmentDoesNotConsumeManaOrDay)
{
	start();
	const auto spell = SpellID(SpellID::decode("core:dimensionDoor"));
	const auto destination = origin + int3(4, 0, 0);
	map()->setProtectedAdventureMobilityTiles({origin + int3(2, 0, 0)});
	const auto before = hero->getManaAvailable();
	EXPECT_FALSE(cast(spell, destination));
	EXPECT_EQ(hero->getManaAvailable(), before);
	EXPECT_EQ(hero->movementPointsRemaining(), 2000u);
	EXPECT_FALSE(hero->hasNewHorizonsAdventureSpellCastToday());
	EXPECT_EQ(hero->visitablePos(), origin);
	map()->setProtectedAdventureMobilityTiles({});
	ASSERT_TRUE(cast(spell, destination));
	EXPECT_EQ(hero->getManaAvailable(), before - 80);
	EXPECT_EQ(hero->movementPointsRemaining(), 0u);
	EXPECT_EQ(hero->visitablePos(), destination);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
}

TEST_F(NewHorizonsProtectedMobilityTest, SavedAbsencePreservesPriorFlyAndDimensionDoorBarrierBehavior)
{
	start(false);
	map()->setProtectedAdventureMobilityTiles({origin + int3(1, 0, 0)});
	fly();
	ASSERT_TRUE(move(origin + int3(1, 0, 0), EPathfindingLayer::AIR));
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
}

TEST_F(NewHorizonsProtectedMobilityTest, ActualWaterWalkAndLandLandingRemainOutsideFlightBarriers)
{
	start();
	const auto water = origin + int3(1, 0, 0);
	const auto land = water + int3(1, 0, 0);
	map()->getTile(water).terrainType = ETerrainId::WATER;
	map()->setProtectedAdventureMobilityTiles({water, land});
	ASSERT_TRUE(cast(SpellID(SpellID::decode("core:waterWalk"))));
	ASSERT_TRUE(move(water, EPathfindingLayer::WATER));
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
	ASSERT_TRUE(move(land, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->visitablePos(), land);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
}

TEST_F(NewHorizonsProtectedMobilityTest, TypedReceiptRejectsStaleSourceAndForgedSurfaceBeforeMutation)
{
	start();
	TryMoveHero packet;
	packet.id = hero->id;
	packet.start = hero->pos;
	packet.end = hero->convertFromVisitablePos(origin + int3(1, 0, 0));
	packet.result = TryMoveHero::SUCCESS;
	packet.movePoints = hero->movementPointsRemaining();
	packet.protectedFlight = newHorizonsProtectedMobility::FlightReceipt{};
	EXPECT_THROW(handler->sendAndApply(packet), std::runtime_error);
	EXPECT_EQ(hero->visitablePos(), origin);
	EXPECT_EQ(hero->movementPointsRemaining(), 2000u);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
	fly();
	packet.protectedFlight->source = EPathfindingLayer::AIR;
	EXPECT_THROW(handler->sendAndApply(packet), std::runtime_error);
	EXPECT_EQ(hero->visitablePos(), origin);
}

TEST_F(NewHorizonsProtectedMobilityTest, PacketCurrentRoundtripAndOldPrefixRejectionAndReset)
{
	start();
	TryMoveHero packet;
	packet.id = hero->id;
	packet.start = hero->pos;
	packet.end = hero->convertFromVisitablePos(origin + int3(1, 0, 0));
	packet.result = TryMoveHero::SUCCESS;
	packet.protectedFlight = newHorizonsProtectedMobility::FlightReceipt{};
	CMemorySerializer current;
	current.oser & packet;
	TryMoveHero restored;
	current.iser & restored;
	ASSERT_TRUE(restored.protectedFlight);
	EXPECT_EQ(restored.protectedFlight->source, EPathfindingLayer::LAND);
	EXPECT_EQ(restored.protectedFlight->destination, EPathfindingLayer::AIR);
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS) - 1);
	EXPECT_THROW(old.oser & packet, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	packet.protectedFlight.reset();
	old.oser & packet;
	old.iser.version = old.oser.version;
	old.iser & restored;
	EXPECT_FALSE(restored.protectedFlight);
}

TEST_F(NewHorizonsProtectedMobilityTest, MapWorldAndLobbyRejectOldMetadataBeforePrefixes)
{
	start(false);
	map()->setProtectedAdventureMobilityTiles({origin + int3(1, 0, 0)});
	const auto previous = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS) - 1);
	CMemorySerializer mapSave;
	mapSave.oser.version = previous;
	EXPECT_THROW(mapSave.oser & *map(), std::runtime_error);
	EXPECT_TRUE(mapSave.extractBuffer().empty());
	CMemorySerializer worldSave;
	worldSave.oser.version = previous;
	EXPECT_THROW(worldSave.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(worldSave.extractBuffer().empty());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	CMemorySerializer lobbySave;
	lobbySave.oser.version = previous;
	EXPECT_THROW(lobbySave.oser & lobby, std::runtime_error);
	EXPECT_TRUE(lobbySave.extractBuffer().empty());
}

TEST_F(NewHorizonsProtectedMobilityTest, CurrentHeroFlightRoundtripAndInvalidEnumRejectWithoutChangingSurface)
{
	start();
	fly();
	ASSERT_TRUE(move(origin + int3(1, 0, 0), EPathfindingLayer::AIR));
	auto restored = CMemorySerializer::deepCopy(*hero, gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getProtectedAdventureFlightLayer(), EPathfindingLayer::AIR);
	EXPECT_EQ(restored->visitablePos(), hero->visitablePos());
	EXPECT_EQ(restored->movementPointsRemaining(), hero->movementPointsRemaining());
	EXPECT_THROW(hero->setProtectedAdventureFlightLayer(EPathfindingLayer::NUM_LAYERS), std::runtime_error);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::AIR);
	CMemorySerializer older;
	older.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS) - 1);
	EXPECT_THROW(older.oser & *hero, std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty());
	ChangeObjPos displacement;
	displacement.objid = hero->id;
	displacement.nPos = hero->visitablePos() + int3(0, 1, 0);
	handler->sendAndApply(displacement);
	EXPECT_EQ(hero->getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
}

TEST_F(NewHorizonsProtectedMobilityTest, CapturedBattleRulesRejectOldPrefixesAndPlainBattleStartReaderIsSafe)
{
	start();
	BattleStart packet;
	packet.info = std::make_unique<BattleInfo>(gameState().get());
	ASSERT_TRUE(newHorizonsProtectedMobility::enabled(packet.info->getMagicRules()));
	const auto previous = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS) - 1);
	CMemorySerializer battle;
	battle.oser.version = previous;
	EXPECT_THROW(battle.oser & *packet.info, std::runtime_error);
	EXPECT_TRUE(battle.extractBuffer().empty());
	CMemorySerializer enclosing;
	enclosing.oser.version = previous;
	EXPECT_THROW(enclosing.oser & packet, std::runtime_error);
	EXPECT_TRUE(enclosing.extractBuffer().empty());
	packet.info.reset();
	CMemorySerializer plain;
	ASSERT_NO_THROW(plain.oser & packet);
	BattleStart restored;
	ASSERT_NO_THROW(plain.iser & restored);
	EXPECT_FALSE(restored.info);
}

TEST_F(NewHorizonsProtectedMobilityTest, OldPlainHeroReadResetsPreloadedFlightLayerWithoutInventingFeature)
{
	start(false);
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS) - 1);
	ASSERT_NO_THROW(old.oser & *hero);
	CGHeroInstance restored(gameState().get());
	restored.setProtectedAdventureFlightLayer(EPathfindingLayer::AIR);
	old.iser.version = old.oser.version;
	old.iser.cb = gameState().get();
	ASSERT_NO_THROW(old.iser & restored);
	EXPECT_EQ(restored.getProtectedAdventureFlightLayer(), EPathfindingLayer::LAND);
	EXPECT_FALSE(newHorizonsProtectedMobility::enabled(restored.getMagicRules()));
}

TEST(NewHorizonsProtectedMobilityRulesTest, VmapActualFullAndHeaderOnlyReadPreserveAuthoredMetadata)
{
	auto map = std::make_unique<CMap>(nullptr);
	map->width = 4;
	map->height = 4;
	map->mapLayers = {MapLayerId::SURFACE};
	map->initTerrain();
	for(int x = 0; x < 4; ++x)
		for(int y = 0; y < 4; ++y)
			map->getTile({x, y, 0}).terrainType = ETerrainId::GRASS;
	map->setProtectedAdventureMobilityTiles({{2, 2, 0}, {1, 1, 0}});
	CMemoryBuffer bytes;
	{
		CMapSaverJson saver(&bytes);
		saver.saveMap(map);
	}
	bytes.seek(0);
	CMapLoaderJson headerLoader(&bytes);
	EXPECT_NO_THROW(headerLoader.loadMapHeader());
	bytes.seek(0);
	CMapLoaderJson fullLoader(&bytes);
	auto restored = fullLoader.loadMap(nullptr);
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->getProtectedAdventureMobilityTiles(), map->getProtectedAdventureMobilityTiles());
	EXPECT_EQ(fullLoader.fileVersionMajor, 5);
	map->setProtectedAdventureMobilityTiles({});
	CMemoryBuffer empty;
	{
		CMapSaverJson saver(&empty);
		saver.saveMap(map);
	}
	empty.seek(0);
	CMapLoaderJson emptyLoader(&empty);
	auto legacy = emptyLoader.loadMap(nullptr);
	ASSERT_NE(legacy, nullptr);
	EXPECT_TRUE(legacy->getProtectedAdventureMobilityTiles().empty());
	EXPECT_EQ(emptyLoader.fileVersionMajor, 3);
}

TEST(NewHorizonsProtectedMobilityRulesTest, ActualHeaderRejectsOlderVersionAndMalformedProtectedTriples)
{
	auto map = std::make_unique<CMap>(nullptr);
	map->width = 4;
	map->height = 4;
	map->mapLayers = {MapLayerId::SURFACE};
	map->initTerrain();
	for(int x = 0; x < 4; ++x)
		for(int y = 0; y < 4; ++y)
			map->getTile({x, y, 0}).terrainType = ETerrainId::GRASS;
	map->setProtectedAdventureMobilityTiles({{1, 1, 0}});
	CMemoryBuffer valid;
	{
		CMapSaverJson saver(&valid);
		saver.saveMap(map);
	}
	valid.seek(0);
	auto io = std::make_shared<CProxyROIOApi>(&valid);
	CZipLoader archive("", "_", io);
	auto bytes = archive.load(JsonPath::builtin(CMapFormatJson::HEADER_FILE_NAME))->readAll();
	JsonNode header(reinterpret_cast<const std::byte *>(bytes.first.get()), bytes.second, "protected-header");
	ASSERT_EQ(header["versionMajor"].Integer(), 5);
	for(int failure = 0; failure < 3; ++failure)
	{
		auto malformed = header;
		if(failure == 0)
			malformed["versionMajor"].Float() = 4;
		if(failure == 1)
			malformed["protectedAdventureMobilityTiles"].Vector()[0].Vector()[0].Float() = 1.5;
		if(failure == 2)
			malformed["protectedAdventureMobilityTiles"].Vector().push_back(malformed["protectedAdventureMobilityTiles"].Vector()[0]);
		CMemoryBuffer invalid;
		{
			CMapSaverJson saver(&invalid);
			saver.addToArchive(malformed, CMapFormatJson::HEADER_FILE_NAME);
		}
		invalid.seek(0);
		CMapLoaderJson loader(&invalid);
		EXPECT_THROW(loader.loadMapHeader(), std::runtime_error);
	}
}
