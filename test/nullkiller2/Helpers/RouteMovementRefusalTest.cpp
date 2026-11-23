/* RouteMovementRefusalTest.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../mock/GameHandlerTestServer.h"
#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Goals/AbstractGoal.h"
#include "lib/GameLibrary.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/entities/hero/CHeroHandler.h"
#include "lib/entities/hero/NewHorizonsPerkRules.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/PacksForClient.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"
#include "server/CGameHandler.h"
#include "server/processors/TurnOrderProcessor.h"
#include "server/queries/CQuery.h"
#include "server/queries/QueriesProcessor.h"

#include <mutex>
#include <shared_mutex>
#include <stdexcept>

// A route is a chain of single-tile MoveHero commands. When the authority refuses one,
// the hero stays put and every later step of the same path no longer neighbours it.
// In AI-only games a battle's deferred level-up reaches the AI just after it sent the
// next step, so the authority refuses that step ("has to answer queries") and the old
// route kept submitting the remaining steps ("Tiles A and B are not neighboring!").
namespace
{
constexpr PlayerColor PLAYER(0);
constexpr PlayerColor OPPONENT(1);

class RefusalServer final : public GameHandlerTestServer
{
public:
	explicit RefusalServer(std::shared_ptr<CGameState> state)
		: GameHandlerTestServer(std::move(state), PLAYER) {}
	NK2AI::AIGateway * gateway = nullptr;
	std::vector<bool> acknowledgements;
	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(auto * response = dynamic_cast<PackageApplied *>(&pack))
		{
			acknowledgements.push_back(response->result);
			if(gateway)
				gateway->requestRealized(response);
		}
	}
	void applyPack(CPackForClient & pack) override
	{
		GameHandlerTestServer::applyPack(pack); // Applied first, like the client.
		if(!gateway)
			return;
		if(auto * move = dynamic_cast<TryMoveHero *>(&pack))
			gateway->heroMoved(*move, true);
		if(auto * levelUp = dynamic_cast<HeroLevelUp *>(&pack))
			forwardLevelUp(*levelUp);
	}
	// Same routing as ApplyClientNetPackVisitor::visitHeroLevelUp.
	void forwardLevelUp(HeroLevelUp & pack)
	{
		const auto * hero = gateway->cc->getHero(pack.heroId);
		ASSERT_NE(hero, nullptr);
		if(newHorizonsHeroes::usesFourOfferProgression(hero->getPerkState().getRules()))
			return gateway->heroGotFourOfferLevel(hero, pack.primskill, pack.fourOffers, pack.queryID,
				pack.optionalRerollAvailable, pack.replacesQueryID, pack.reachedLevel);
		auto skills = pack.skills;
		gateway->heroGotLevelWithReroll(hero, pack.primskill, skills, pack.perks, pack.queryID,
			pack.optionalRerollAvailable, pack.replacesQueryID, pack.reachedLevel);
	}
};

class RefusalClient final : public IClient
{
	CGameHandler & handler;
	int sequence = 0;
public:
	explicit RefusalClient(CGameHandler & handler) : handler(handler) {}
	NK2AI::AIGateway * gateway = nullptr;
	const CGHeroInstance * hero = nullptr;
	std::mutex transport; // Serializes the AI query worker like one connection.
	int moves = 0;
	int replies = 0;
	int nonNeighbouringMoves = 0;
	std::function<void()> beforeFirstMove;
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		std::lock_guard serialized(transport);
		if(const auto * move = dynamic_cast<const MoveHero *>(&request))
		{
			// The authority's STANDARD-mode neighbour rule, evaluated where the hero actually stands.
			const auto & destination = move->path.front();
			if(destination != hero->pos && !hero->pos.areNeighbours(destination))
				++nonNeighbouringMoves;
			if(moves++ == 0 && beforeFirstMove)
				beforeFirstMove();
		}
		else if(dynamic_cast<const QueryReply *>(&request))
			++replies;
		else
			throw std::runtime_error("Route fixture submitted an unsupported request");
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++sequence;
		if(gateway)
			gateway->requestSent(incoming.get(), sequence);
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return sequence;
	}
};

class RouteMovementRefusalTest : public TinyMapGameTest
{
protected:
	CGHeroInstance * hero = nullptr;
	std::unique_ptr<RefusalServer> server;
	std::unique_ptr<CGameHandler> handler;
	std::unique_ptr<RefusalClient> client;
	std::shared_ptr<CCallback> callback;
	std::unique_ptr<NK2AI::AIGateway> gateway;
	const int3 heroPos{8, 8, 0};
	int3 start; // Visitable tile, left of the hero's anchor.
	int3 target;
	Services * gameServices() override { return LIBRARY; }
	void TearDown() override
	{
		if(gateway)
			gateway->finish();
		if(server)
			server->gateway = nullptr;
		if(client)
			client->gateway = nullptr;
		gateway.reset();
		callback.reset();
		client.reset();
		handler.reset();
		server.reset();
		TinyMapGameTest::TearDown();
	}
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(OPPONENT)
			.hero(heroPos, HeroTypeID(0), PLAYER).hero({30, 30, 0}, HeroTypeID(2), OPPONENT);
		startWithMap(std::move(builder));
		for(int x = 0; x < map()->width; ++x)
			for(int y = 0; y < map()->height; ++y)
				map()->getTile({x, y, 0}).terrainType = ETerrainId::GRASS;
		hero = findHeroAt(heroPos);
		ASSERT_NE(hero, nullptr);
		start = hero->visitablePos();
		target = start + int3(3, 0, 0);
		revealMap(PLAYER);
		server = std::make_unique<RefusalServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->turnOrder->addPlayer(PLAYER);
		handler->turnOrder->addPlayer(OPPONENT);
		handler->onNewTurn();
		handler->turnOrder->onGameStarted();
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(PLAYER));
		handler->uiReadyForDialogs.insert(PLAYER);
		client = std::make_unique<RefusalClient>(*handler);
		client->hero = hero;
		callback = makeCallback(PLAYER, client.get());
		gateway = std::make_unique<NK2AI::AIGateway>();
		gateway->initGameInterface(std::shared_ptr<Environment>(), callback);
		gateway->status.startedTurn();
		server->gateway = client->gateway = gateway.get();
		hero->setMovementPoints(1000); // The whole three-step route fits in the current day.
	}
	bool moveToTarget()
	{
		// Same ownership as makeTurn: request realization and waitTillFree run under this lease.
		std::shared_lock lock(CGameState::mutex);
		return gateway->moveHeroToTile(target, NK2AI::HeroPtr(hero, gateway->cc.get()));
	}
};
}

TEST_F(RouteMovementRefusalTest, LevelUpDeliveredBehindFirstStepIsAnsweredThenTheRouteContinues)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto level = hero->level;
	client->beforeFirstMove = [&]()
	{
		// A won battle issues its deferred level-up after the AI already chose this step.
		handler->giveExperience(hero, LIBRARY->heroh->reqExp(level + 1) - hero->exp);
		EXPECT_NE(handler->queries->topQuery(PLAYER), nullptr);
	};
	bool reached = false;
	EXPECT_NO_THROW(reached = moveToTarget());
	EXPECT_TRUE(reached);
	EXPECT_EQ(client->nonNeighbouringMoves, 0);
	EXPECT_EQ(hero->visitablePos(), target);
	EXPECT_EQ(hero->level, level + 1);
	EXPECT_EQ(client->replies, 1);
	EXPECT_EQ(client->moves, 4); // The refused first step, its resubmission, then the remaining two.
	EXPECT_EQ(server->acknowledgements, (std::vector<bool>{false, true, true, true, true}));
	EXPECT_EQ(handler->queries->topQuery(PLAYER), nullptr);
	EXPECT_EQ(gateway->status.getQueriesCount(), 0);
}

TEST_F(RouteMovementRefusalTest, RefusedStepStopsTheRouteInsteadOfSendingNonNeighbouringSteps)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	std::shared_ptr<CGenericQuery> unseen;
	client->beforeFirstMove = [&]()
	{
		// An authority-side interaction the AI never learns about: the refusal is not transient.
		unseen = std::make_shared<CGenericQuery>(handler.get(), PLAYER, [](std::optional<int32_t>) {});
		handler->queries->addQuery(unseen);
	};
	EXPECT_THROW(moveToTarget(), NK2AI::cannotFulfillGoalException);
	ASSERT_NE(unseen, nullptr);
	handler->queries->popQuery(unseen);
	EXPECT_EQ(client->nonNeighbouringMoves, 0);
	EXPECT_EQ(client->moves, 1);
	EXPECT_EQ(server->acknowledgements, (std::vector<bool>{false}));
	EXPECT_EQ(hero->visitablePos(), start);
}
