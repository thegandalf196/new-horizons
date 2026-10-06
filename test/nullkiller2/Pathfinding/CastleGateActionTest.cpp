/*
 * CastleGateActionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Pathfinding/Actions/CastleGateAction.h"
#include "AI/Nullkiller2/Pathfinding/Actions/DimensionDoorAction.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "SpellPointTestUtils.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/callback/IClient.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "lib/spells/NewHorizonsMagic.h"
#include "lib/spells/CSpell.h"
#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"
#include "server/CGameHandler.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr PlayerColor OTHER_PLAYER(1);
using namespace NK2AI;

SpellID spell(const char * identity)
{
	return SpellID(SpellID::decode(identity));
}

class CastleGateLoopbackClient final : public IClient
{
	CGameHandler & handler;
	int lastRequestId = 0;

public:
	int castleGateRequests = 0;

	explicit CastleGateLoopbackClient(CGameHandler & handler)
		: handler(handler)
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
		if(!dynamic_cast<const CastleTeleportHero *>(&request))
			throw std::runtime_error("Castle Gate AI test expected a CastleTeleportHero request");

		auto serverRequest = CMemorySerializer::deepCopy(request);
		serverRequest->player = player;
		serverRequest->requestID = ++lastRequestId;
		++castleGateRequests;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *serverRequest);
		return lastRequestId;
	}
};

BuildingID castleGateBuilding(const CGTownInstance & town)
{
	const auto it = std::ranges::find_if(town.getTown()->buildings, [](const auto & entry)
	{
		return entry.second->subId == BuildingSubID::CASTLE_GATE;
	});
	return it == town.getTown()->buildings.end() ? BuildingID::NONE : it->first;
}

class CastleGateActionTest : public NullkillerTest
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

	void startGateMap()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsCastleGateAI")
			.playerActive(PLAYER)
			.playerActive(OTHER_PLAYER)
			.town({4, 4, 0}, FactionID::INFERNO, PLAYER)
			.town({14, 4, 0}, FactionID::INFERNO, PLAYER)
			.town({28, 28, 0}, FactionID::CASTLE, OTHER_PLAYER)
			.hero({8, 8, 0}, HeroTypeID(0), PLAYER)
			.heroGarrison({{CreatureID(27), 1}})
			.heroPrimary(10, 10, 10, 20)
			.heroSpells({spell("core:dimensionDoor")})
			.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
			.hero({30, 30, 0}, HeroTypeID(1), OTHER_PLAYER);
		startWithMap(std::move(builder));
		revealMap(PLAYER);

		sourceTown = dynamic_cast<CGTownInstance *>(findObjectAt({4, 4, 0}));
		destinationTown = dynamic_cast<CGTownInstance *>(findObjectAt({14, 4, 0}));
		hero = findHeroByOwner(PLAYER);
		ASSERT_NE(sourceTown, nullptr);
		ASSERT_NE(destinationTown, nullptr);
		ASSERT_NE(hero, nullptr);
		setTestSpellPointTotal(hero, 200);

		const auto sourceGate = castleGateBuilding(*sourceTown);
		const auto destinationGate = castleGateBuilding(*destinationTown);
		ASSERT_NE(sourceGate, BuildingID::NONE);
		ASSERT_NE(destinationGate, BuildingID::NONE);
		sourceTown->addBuilding(sourceGate);
		destinationTown->addBuilding(destinationGate);
		ASSERT_TRUE(sourceTown->hasBuilt(BuildingSubID::CASTLE_GATE));
		ASSERT_TRUE(destinationTown->hasBuilt(BuildingSubID::CASTLE_GATE));

		gameState()->actingPlayers.insert(PLAYER);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		const auto sourceHeroPosition = hero->convertFromVisitablePos(sourceTown->visitablePos());
		ASSERT_TRUE(handler->moveHero(hero->id, sourceHeroPosition, EMovementMode::TOWN_PORTAL));
		ASSERT_EQ(hero->getVisitedTown(), sourceTown);
		handler->setMovePoints(hero->id, 1000);
		ASSERT_GT(hero->movementPointsRemaining(), 0);
		ASSERT_TRUE(vstd::contains(gameState()->actingPlayers, PLAYER))
			<< "the active player must remain in-turn after source-town setup";
	}

	std::vector<AIPath> pathsTo(AIGateway & gateway, const int3 & target)
	{
		HeroMap<HeroRole> heroes;
		heroes.emplace(hero, MAIN);
		PathfinderSettings settings;
		settings.useHeroChain = false;
		gateway.nullkiller->pathfinder->updatePaths(heroes, settings);
		return gateway.nullkiller->pathfinder->getPathInfo(target);
	}

	CGTownInstance * sourceTown = nullptr;
	CGTownInstance * destinationTown = nullptr;
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
};
}

TEST_F(CastleGateActionTest, AIPathUsesOwnedInfernoGateAndExecutesAuthoritativeTeleport)
{
	startGateMap();
	ASSERT_TRUE(newHorizonsMagic::rulesActive(gameState()->getMagicRules()));

	CastleGateLoopbackClient client(*handler);
	auto callback = makeCallback(PLAYER, &client);
	auto gateway = makeGateway(callback);
	const auto currentDay = gameState()->getCalendar().getCurrentDay();
	hero->markNewHorizonsCastleGateUsed(currentDay);
	const auto alreadyUsedPaths = pathsTo(*gateway, destinationTown->visitablePos());
	EXPECT_FALSE(std::ranges::any_of(alreadyUsedPaths, [](const AIPath & path)
	{
		return std::ranges::any_of(path.nodes, [](const AIPathNodeInfo & node)
		{
			return node.turns == 0
				&& node.specialAction
				&& dynamic_cast<const AIPathfinding::CastleGateAction *>(node.specialAction.get());
		});
	})) << "a Castle Gate already used today must not be planned again on the current day";
	hero->markNewHorizonsCastleGateUsed(-1);

	const auto paths = pathsTo(*gateway, destinationTown->visitablePos());
	const auto gatePath = std::ranges::find_if(paths, [](const AIPath & path)
	{
		return std::ranges::any_of(path.nodes, [](const AIPathNodeInfo & node)
		{
			return node.specialAction
				&& dynamic_cast<const AIPathfinding::CastleGateAction *>(node.specialAction.get())
				&& !node.actionIsBlocked;
		});
	});
	ASSERT_NE(gatePath, paths.end()) << "Nullkiller did not produce an executable Castle Gate route";
	EXPECT_EQ(gatePath->targetNode().turns, 0);

	const auto storage = gateway->nullkiller->pathfinder->getStorage();
	ASSERT_NE(storage, nullptr);
	bool foundDimensionDoorFromGateTown = false;
	for(int x = 0; x < map()->width; ++x)
	{
		for(int y = 0; y < map()->height; ++y)
		{
			storage->iterateValidNodes({x, y, 0}, EPathfindingLayer::LAND, [&](const AIPathNode & node)
			{
				if(node.specialAction
					&& dynamic_cast<const AIPathfinding::DimensionDoorAction *>(node.specialAction.get())
					&& node.theNodeBefore
					&& node.theNodeBefore->coord == sourceTown->visitablePos())
				{
					foundDimensionDoorFromGateTown = true;
					EXPECT_FALSE(storage->isObjectTeleportation(&node));
				}
			});
		}
	}
	EXPECT_TRUE(foundDimensionDoorFromGateTown)
		<< "the gated-town source should retain ordinary Dimension Door movement accounting";

	const auto actionNode = std::ranges::find_if(gatePath->nodes, [](const AIPathNodeInfo & node)
	{
		return node.specialAction
			&& dynamic_cast<const AIPathfinding::CastleGateAction *>(node.specialAction.get())
			&& !node.actionIsBlocked;
	});
	ASSERT_NE(actionNode, gatePath->nodes.end());
	actionNode->specialAction->execute(gateway.get(), hero);

	EXPECT_EQ(client.castleGateRequests, 1);
	EXPECT_EQ(hero->getVisitedTown(), destinationTown);
	EXPECT_EQ(hero->movementPointsRemaining(), 0);
	EXPECT_TRUE(hero->hasUsedNewHorizonsCastleGateToday(currentDay));

	const auto returnPaths = pathsTo(*gateway, sourceTown->visitablePos());
	const auto tomorrowGatePath = std::ranges::find_if(returnPaths, [](const AIPath & path)
	{
		return std::ranges::any_of(path.nodes, [](const AIPathNodeInfo & node)
		{
			return node.turns == 1
				&& node.specialAction
				&& dynamic_cast<const AIPathfinding::CastleGateAction *>(node.specialAction.get())
				&& !node.actionIsBlocked;
		});
	});
	ASSERT_NE(tomorrowGatePath, returnPaths.end())
		<< "the spent gate should become available in the next planned day, including after MP rollover";
	EXPECT_FALSE(std::ranges::any_of(returnPaths, [](const AIPath & path)
	{
		return std::ranges::any_of(path.nodes, [](const AIPathNodeInfo & node)
		{
			return node.turns == 0
				&& node.specialAction
				&& dynamic_cast<const AIPathfinding::CastleGateAction *>(node.specialAction.get());
		});
	})) << "an already-used Castle Gate must not be planned again today";
}
