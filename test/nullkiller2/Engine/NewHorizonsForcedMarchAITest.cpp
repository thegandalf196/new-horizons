/*
 * NewHorizonsForcedMarchAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Pathfinding/AIPathfinder.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/entities/hero/NewHorizonsPerkState.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/pathfinder/CPathfinder.h"
#include "lib/pathfinder/PathfinderOptions.h"
#include "lib/serializer/CMemorySerializer.h"
#include "mock/GameHandlerTestServer.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"
#include "server/CGameHandler.h"

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto LOGISTICS_SKILL = "new-horizons:logistics";
constexpr auto FORCED_MARCH_PERK = "new-horizons:logistics.forcedMarch";

void activateMapPerk(JsonNode & rules, std::string_view perkId)
{
	for(auto & [skillId, skill] : rules["skills"].Struct())
		for(auto & perk : skill["perks"].Vector())
			if(perk["id"].String() == perkId)
			{
				perk["effect"]["status"].String() = "active";
				return;
			}
	throw std::runtime_error("Missing New Horizons Logistics perk in AI fixture: " + std::string(perkId));
}

class RecordingPerkServer final : public GameHandlerTestServer
{
public:
	explicit RecordingPerkServer(const std::shared_ptr<CGameState> & state)
		: GameHandlerTestServer(state, PLAYER)
	{}

	std::vector<std::string> acceptedPerks;

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * chosen = dynamic_cast<const HeroPerkChosen *>(&pack))
			acceptedPerks.push_back(chosen->selection.perkId);
		GameHandlerTestServer::applyPack(pack);
	}
};

class MovementLoopbackClient final : public IClient
{
	CGameHandler & handler;
	int lastRequestId = 0;

public:
	std::vector<int3> submittedDestinations;

	explicit MovementLoopbackClient(CGameHandler & handler)
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
		const auto * move = dynamic_cast<const MoveHero *>(&request);
		if(!move || move->path.size() != 1)
			throw std::runtime_error("Forced March AI fixture expected a single-step MoveHero request");

		auto serverRequest = CMemorySerializer::deepCopy(request);
		serverRequest->player = player;
		serverRequest->requestID = ++lastRequestId;
		submittedDestinations.push_back(move->path.front());
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *serverRequest);
		return lastRequestId;
	}
};

class NewHorizonsForcedMarchAITest : public NullkillerTest
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));

		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		activateMapPerk(perkRules, FORCED_MARCH_PERK);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NewHorizonsForcedMarchAI")
			.playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(16), PLAYER)
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		revealMap(PLAYER);

		hero = findHeroByOwner(PLAYER);
		ASSERT_NE(hero, nullptr);
		ASSERT_TRUE(hero->usesNewHorizonsMovement());

		gameState()->actingPlayers.insert(PLAYER);
		server = std::make_unique<RecordingPerkServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		for(int x = 0; x < 36; ++x)
			for(int y = 0; y < 36; ++y)
			{
				auto & tile = map()->getTile({x, y, 0});
				tile.terrainType = ETerrainId::GRASS;
				tile.roadType = RoadId::NO_ROAD;
			}
	}

	SecondarySkill logisticsSkill() const
	{
		const int decoded = SecondarySkill::decode(LOGISTICS_SKILL);
		if(decoded < 0)
			throw std::runtime_error("New Horizons Logistics skill is not registered");
		return SecondarySkill(decoded);
	}

	void advanceLogistics()
	{
		const int currentRank = hero->getPerkSkillRank(LOGISTICS_SKILL);
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(LOGISTICS_SKILL, currentRank));
		handler->levelUpHero(hero, logisticsSkill(), false);
	}

	bool chooseOfferedPerk(std::string_view perkId)
	{
		const auto rankLookup = [this](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto candidate = std::find_if(offer.begin(), offer.end(), [perkId](const auto & entry)
			{
				return entry.selection.perkId == perkId;
			});
			if(candidate == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), candidate));
			handler->levelUpHero(hero, offer, choice, seed, false);
			return true;
		}
		return false;
	}

	CGHeroInstance * hero = nullptr;
	std::unique_ptr<RecordingPerkServer> server;
	std::unique_ptr<CGameHandler> handler;
};
} // namespace

TEST_F(NewHorizonsForcedMarchAITest, GatewayUsesFreshPathsToSpendMovementAwardedOnExhaustion)
{
	startGame();
	advanceLogistics();
	ASSERT_EQ(hero->getPerkSkillRank(LOGISTICS_SKILL), MasteryLevel::BASIC);
	ASSERT_TRUE(chooseOfferedPerk(FORCED_MARCH_PERK));
	ASSERT_TRUE(hero->hasActivePerk(LOGISTICS_SKILL, FORCED_MARCH_PERK));
	ASSERT_EQ(server->acceptedPerks.back(), FORCED_MARCH_PERK);

	const auto source = hero->visitablePos();
	const auto firstTile = source + int3(1, 0, 0);
	const auto extraOnlyTile = source + int3(2, 0, 0);
	for(const auto & pos : {source, firstTile, extraOnlyTile})
		map()->getTile(pos).roadType = RoadId::DIRT_ROAD;

	MovementLoopbackClient client(*handler);
	auto callback = makeCallback(PLAYER, &client);
	auto gateway = makeGateway(callback);
	NK2AI::Goals::TGoalVec priorityTasks;
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 1));

	const int maximumMovement = hero->movementPointsLimit();
	ASSERT_GT(maximumMovement, 0);
	const int expectedExtraMovement = maximumMovement / 10;
	ASSERT_GT(expectedExtraMovement, 0);
	PathfinderOptions options(*callback);
	CPathfinderHelper movement(*callback, hero, options);
	const int firstStepCost = movement.getMovementCost(source, firstTile, EPathfindingLayer::LAND, maximumMovement);
	ASSERT_GT(firstStepCost, 0);
	ASSERT_LE(firstStepCost, expectedExtraMovement);

	// Model the last ordinary step of the day. The next tile is not reachable
	// in the normal remainder, but will be after the authoritative burst.
	hero->setMovementPoints(firstStepCost);
	gateway->invalidatePaths();
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 2));
	const auto normalRemainderPath = gateway->nullkiller->pathfinder->getPathInfo(extraOnlyTile);
	ASSERT_FALSE(normalRemainderPath.empty());
	int earliestNormalTurn = std::numeric_limits<int>::max();
	for(const auto & path : normalRemainderPath)
		earliestNormalTurn = std::min<int>(earliestNormalTurn, path.turn());
	EXPECT_GT(earliestNormalTurn, 0);

	const int currentDay = gameState()->getCalendar().getCurrentDay();
	ASSERT_TRUE(gateway->moveHeroToTile(firstTile, NK2AI::HeroPtr(hero, callback.get())));
	EXPECT_EQ(hero->visitablePos(), firstTile);
	EXPECT_EQ(hero->movementPointsRemaining(), expectedExtraMovement);
	EXPECT_EQ(hero->getNewHorizonsForcedMarchLastUseDay(), currentDay);
	EXPECT_EQ(hero->getNewHorizonsForcedMarchPenaltyDay(), currentDay);

	gateway->invalidatePaths();
	ASSERT_TRUE(gateway->nullkiller->updateStateAndExecutePriorityPass(priorityTasks, 3));
	const auto freshExtraPath = gateway->nullkiller->pathfinder->getPathInfo(extraOnlyTile);
	ASSERT_FALSE(freshExtraPath.empty());
	int earliestFreshTurn = std::numeric_limits<int>::max();
	for(const auto & path : freshExtraPath)
		earliestFreshTurn = std::min<int>(earliestFreshTurn, path.turn());
	EXPECT_EQ(earliestFreshTurn, 0);

	ASSERT_TRUE(gateway->moveHeroToTile(extraOnlyTile, NK2AI::HeroPtr(hero, callback.get())));
	EXPECT_EQ(hero->visitablePos(), extraOnlyTile);
	EXPECT_EQ(hero->movementPointsRemaining(), expectedExtraMovement - firstStepCost);
	ASSERT_EQ(client.submittedDestinations.size(), 2u);
	EXPECT_EQ(client.submittedDestinations[0], hero->convertFromVisitablePos(firstTile));
	EXPECT_EQ(client.submittedDestinations[1], hero->convertFromVisitablePos(extraOnlyTile));
}
