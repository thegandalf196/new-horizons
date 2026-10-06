/*
 * NewHorizonsArcaneReservoirAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "lib/CPlayerState.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/ResourceSet.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/gameState/CGameState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"
#include "server/CGameHandler.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr PlayerColor OTHER_PLAYER(1);

class ReservoirVisitLoopbackClient final : public IClient
{
	CGameHandler & handler;
	ObjectInstanceID expectedTown;
	int lastRequestId = 0;

public:
	std::vector<BuildingID> requestedBuildings;

	ReservoirVisitLoopbackClient(CGameHandler & handler, ObjectInstanceID expectedTown)
		: handler(handler)
		, expectedTown(expectedTown)
	{}

	std::optional<BattleAction> makeSurrenderRetreatDecision(
		PlayerColor,
		const BattleID &,
		const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		const auto * visit = dynamic_cast<const VisitTownBuilding *>(&request);
		if(!visit || visit->tid != expectedTown || visit->bid != BuildingID::SPECIAL_4)
			throw std::runtime_error("Arcane Reservoir AI fixture expected a visit to the built Reservoir");

		auto serverRequest = CMemorySerializer::deepCopy(request);
		serverRequest->player = player;
		serverRequest->requestID = ++lastRequestId;
		requestedBuildings.push_back(visit->bid);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *serverRequest);
		return lastRequestId;
	}
};

class NewHorizonsArcaneReservoirAITest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == PLAYER)
			settings.connectedPlayerIDs.clear();
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsArcaneReservoirAI")
			.playerActive(PLAYER)
			.town({5, 5, 0}, FactionID::TOWER, PLAYER)
			.hero({10, 10, 0}, HeroTypeID(0), PLAYER)
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
			.heroPrimary(10, 10, 10, 20)
			.heroGarrison({})
			.playerActive(OTHER_PLAYER)
			.town({26, 26, 0}, FactionID::CASTLE, OTHER_PLAYER)
			.hero({30, 30, 0}, HeroTypeID(1), OTHER_PLAYER);
		startWithMap(std::move(builder));

		town = dynamic_cast<CGTownInstance *>(findObjectAt({5, 5, 0}));
		hero = findHeroByOwner(PLAYER);
		ASSERT_NE(town, nullptr);
		ASSERT_NE(hero, nullptr);
		ASSERT_EQ(town->tempOwner, PLAYER);
		ASSERT_EQ(hero->tempOwner, PLAYER);
		clearResources();
	}

	void clearResources()
	{
		const auto current = gameState()->getPlayerState(PLAYER)->resources;
		for(const auto resource : ALL_RESOURCES)
			grantResources(PLAYER, resource, -static_cast<int>(current[resource]));
	}

	void placeHeroInTown(CGameHandler & handler)
	{
		const auto position = hero->convertFromVisitablePos(town->visitablePos());
		ASSERT_TRUE(handler.moveHero(hero->id, position, EMovementMode::TOWN_PORTAL));
		ASSERT_EQ(hero->getVisitedTown(), town);
		ASSERT_TRUE(vstd::contains(gameState()->actingPlayers, PLAYER));
	}

	void interact(ReservoirVisitLoopbackClient & client)
	{
		auto callback = makeCallback(PLAYER, &client);
		auto gateway = makeGateway(callback);
		gateway->performObjectInteraction(town, NK2AI::HeroPtr(hero, callback.get()));
	}

	CGTownInstance * town = nullptr;
	CGHeroInstance * hero = nullptr;
	static constexpr std::array<GameResID, 7> ALL_RESOURCES = {
		GameResID(EGameResID::WOOD), GameResID(EGameResID::MERCURY), GameResID(EGameResID::ORE),
		GameResID(EGameResID::SULFUR), GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS),
		GameResID(EGameResID::GOLD)
	};
};
}

TEST_F(NewHorizonsArcaneReservoirAITest, BuiltReservoirUsesValidatedVisitAndGrantsBufferOnlyOncePerWeek)
{
	startGame();
	town->addBuilding(BuildingID::SPECIAL_4);
	ASSERT_TRUE(town->hasBuilt(BuildingID::SPECIAL_4));

	gameState()->actingPlayers.insert(PLAYER);
	auto server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
	CGameHandler handler(*server, gameState());
	placeHeroInTown(handler);

	const int normalBefore = hero->manaLimit() - 7;
	ASSERT_GT(normalBefore, 0);
	handler.setManaPoints(hero->id, normalBefore);
	ASSERT_EQ(hero->getNormalSpellPoints(), normalBefore);
	ASSERT_EQ(hero->getBufferSpellPoints(), 0);

	ReservoirVisitLoopbackClient client(handler, town->id);
	interact(client);

	ASSERT_EQ(client.requestedBuildings, std::vector<BuildingID>{BuildingID::SPECIAL_4});
	EXPECT_EQ(hero->getNormalSpellPoints(), normalBefore);
	EXPECT_EQ(hero->getBufferSpellPoints(), 50);
	EXPECT_EQ(hero->getManaAvailable(), normalBefore + 50);

	interact(client);

	EXPECT_EQ(client.requestedBuildings.size(), 1u);
	EXPECT_EQ(hero->getNormalSpellPoints(), normalBefore);
	EXPECT_EQ(hero->getBufferSpellPoints(), 50);
	EXPECT_EQ(hero->getManaAvailable(), normalBefore + 50);
}

TEST_F(NewHorizonsArcaneReservoirAITest, UnbuiltReservoirIsIgnoredAndInactiveVisitIsRejected)
{
	startGame();
	gameState()->actingPlayers.insert(PLAYER);
	auto server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
	CGameHandler handler(*server, gameState());
	placeHeroInTown(handler);
	const int normalBefore = hero->getNormalSpellPoints();

	ReservoirVisitLoopbackClient client(handler, town->id);
	interact(client);

	EXPECT_TRUE(client.requestedBuildings.empty());
	EXPECT_EQ(hero->getBufferSpellPoints(), 0);

	town->addBuilding(BuildingID::SPECIAL_4);
	gameState()->actingPlayers.erase(PLAYER);
	gameState()->actingPlayers.insert(OTHER_PLAYER);
	interact(client);

	EXPECT_EQ(client.requestedBuildings.size(), 1u);
	EXPECT_EQ(hero->getNormalSpellPoints(), normalBefore);
	EXPECT_EQ(hero->getBufferSpellPoints(), 0);
}
