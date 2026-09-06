/*
 * ExperimentalSetFormationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#ifdef NH_PERF_EXPERIMENTS

#include "../server/battles/BattleTestFixture.h"
#include "../../server/CVCMIServer.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/CQuery.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/network/ExperimentalSetFormation.h"
#include "../../lib/network/NetworkInterface.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/GameConnection.h"

#include <cstdlib>
#include <future>

namespace
{
bool formationExperimentEnabled()
{
	const auto * value = std::getenv("NH_PERF_QUEUED_SET_FORMATION");
	return value && std::string(value) == "1";
}

// Actual CVCMIServer routing/visitor, with a deterministic test connection-to-player
// mapping instead of running a graphical lobby. No replacement action validator.
class FormationServer : public CVCMIServer
{
public:
	bool permitConnection = true;
	std::thread::id authorityThread;
	std::vector<std::thread::id> callbacks;

	FormationServer() : CVCMIServer(0, true) {}
	bool hasPlayerAt(PlayerColor player, GameConnectionID connection) const override
	{
		return permitConnection && player == PlayerColor(0) && connection == GameConnectionID::FIRST_CONNECTION;
	}
	void onPacketReceived(const NetworkConnectionPtr & connection, const std::vector<std::byte> & bytes) override
	{
		checkThread();
		CVCMIServer::onPacketReceived(connection, bytes);
	}
	void onExperimentalSetFormation(const NetworkConnectionPtr & connection, SetFormation & pack) override
	{
		checkThread();
		CVCMIServer::onExperimentalSetFormation(connection, pack);
	}
private:
	void checkThread()
	{
		if(std::this_thread::get_id() != authorityThread)
			throw std::runtime_error("Formation dispatched outside authoritative executor");
		callbacks.push_back(std::this_thread::get_id());
	}
};

class BlockingFormationQuery : public CQuery
{
public:
	explicit BlockingFormationQuery(CGameHandler * handler) : CQuery(handler, QueryType::Generic) { addPlayer(PlayerColor(0)); }
	bool blocksPack(const CPackForServer *) const override { return true; }
};

struct ObservedResponse
{
	std::string kind;
	uint32_t request = 0;
	bool result = false;
	ObjectInstanceID hero = ObjectInstanceID::NONE;
	EArmyFormation formation = EArmyFormation::LOOSE;
	bool operator==(const ObservedResponse &) const = default;
};

// Holds the executor at a safe boundary, never in partially applied gameplay.
// RAII releases it even when a test assertion fails.
class ExecutorPause
{
	std::promise<void> release;
public:
	explicit ExecutorPause(NetworkContext & context)
	{
		auto entered = std::make_shared<std::promise<void>>();
		auto ready = entered->get_future();
		auto resume = release.get_future().share();
		boost::asio::post(context, [entered, resume] { entered->set_value(); resume.wait(); });
		if(ready.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
			throw std::runtime_error("Executor pause deadline exceeded");
	}
	~ExecutorPause() { release.set_value(); }
};
}

class ExperimentalSetFormationTest : public BattleTestFixture, public INetworkClientListener
{
protected:
	std::unique_ptr<FormationServer> authority;
	std::unique_ptr<INetworkHandler> clientNetwork;
	NetworkConnectionPtr clientEndpoint, serverEndpoint;
	std::unique_ptr<GameConnection> clientConnection;
	std::thread worker;
	std::exception_ptr workerFailure;
	std::vector<ObservedResponse> responses;

	void onConnectionEstablished(const NetworkConnectionPtr & connection) override
	{
		clientEndpoint = connection;
		clientConnection = std::make_unique<GameConnection>(connection);
		clientConnection->setCallback(*gameState());
	}
	void onConnectionFailed(const std::string & message) override { throw std::runtime_error(message); }
	void onDisconnected(const NetworkConnectionPtr &, const std::string &) override {}
	void onPacketReceived(const NetworkConnectionPtr & connection, const std::vector<std::byte> & bytes) override
	{
		if(connection != clientEndpoint)
			throw std::runtime_error("Response delivered on wrong client endpoint");
		auto pack = clientConnection->retrievePack(bytes); // Responses always take the byte path.
		if(auto * received = dynamic_cast<PackageReceived *>(pack.get()))
			responses.push_back({"received", received->requestID});
		else if(auto * applied = dynamic_cast<PackageApplied *>(pack.get()))
			responses.push_back({"applied", applied->requestID, applied->result});
		else if(auto * changed = dynamic_cast<ChangeFormation *>(pack.get()))
			responses.push_back({"formation", 0, false, changed->hid, changed->formation});
		else
			responses.push_back({typeid(*pack).name()});
	}

	template<typename Action> void onAuthority(Action action)
	{
		auto done = std::make_shared<std::promise<void>>();
		auto ready = done->get_future();
		boost::asio::post(authority->getNetworkHandler().getContext(), [done, action]() mutable
		{
			try { action(); done->set_value(); }
			catch(...) { done->set_exception(std::current_exception()); }
		});
		if(ready.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
			throw std::runtime_error("Authoritative task deadline exceeded");
		ready.get();
	}

	void drain()
	{
		onAuthority([] {}); // FIFO barrier after requests; all response bytes are now queued.
		clientNetwork->getContext().restart();
		clientNetwork->getContext().poll();
	}

	void stopScenario()
	{
		if(authority) authority->stop();
		if(worker.joinable()) worker.join();
		authority.reset();
		clientConnection.reset();
		clientEndpoint.reset(); serverEndpoint.reset();
		clientNetwork.reset();
		if(workerFailure)
		{
			auto failure = std::exchange(workerFailure, {});
			std::rethrow_exception(failure);
		}
	}

	void startScenario()
	{
		stopScenario();
		if(map())
		{
			// Each comparison arm needs a fresh map fixture, not a second load
			// into the previous arm's state and MapListener registration.
			server.gameState.reset();
			BattleTestFixture::TearDown();
			TinyMapGameTest::SetUp();
		}
		startGame();
		gameHandler.reset(); // Replace only the fixture's IGameServer, not its real game state.
		authority = std::make_unique<FormationServer>();
		authority->prepare(false, false); // Internal wrapper; no socket/listen.
		clientNetwork = INetworkHandler::createHandler();
		clientNetwork->createInternalConnection(*this, authority->getNetworkServer());
		clientNetwork->getContext().poll();
		ASSERT_NE(clientConnection, nullptr);
		ASSERT_EQ(authority->activeConnections.size(), 1u);
		authority->activeConnections.front()->connectionID = GameConnectionID::FIRST_CONNECTION;
		serverEndpoint = authority->activeConnections.front()->getConnection();
		authority->gh = std::make_shared<CGameHandler>(*authority, gameState());
		worker = std::thread([this]
		{
			authority->authorityThread = std::this_thread::get_id();
			try { authority->run(); }
			catch(...) { workerFailure = std::current_exception(); }
		});
		onAuthority([this]
		{
			authority->setState(EServerState::GAMEPLAY);
			PlayerStartsTurn turn;
			turn.player = PlayerColor(0);
			authority->gh->sendAndApply(turn);
		});
		drain(); responses.clear();
	}

	SetFormation request(uint32_t id, EArmyFormation formation) const
	{
		SetFormation pack(attackerSideHero->id, formation);
		pack.player = PlayerColor(0);
		pack.requestID = id;
		return pack;
	}

	void TearDown() override
	{
		stopScenario();
		BattleTestFixture::TearDown();
	}
};

TEST_F(ExperimentalSetFormationTest, RealValidationAndResponseEquivalence)
{
	if(!formationExperimentEnabled()) GTEST_SKIP() << "Set NH_PERF_QUEUED_SET_FORMATION=1 for native experimental proofs";
	for(const std::string scenario : {"accepted", "owner", "inactive", "query", "missing_hero", "connection"})
	{
		std::vector<ObservedResponse> baseline;
		for(bool typed : {false, true})
		{
			SCOPED_TRACE(scenario + (typed ? " typed" : " bytes"));
			ASSERT_NO_FATAL_FAILURE(startScenario());
			const auto initial = attackerSideHero->formation;
			const auto target = initial == EArmyFormation::LOOSE ? EArmyFormation::TIGHT : EArmyFormation::LOOSE;
			auto pack = request(7, target);
			if(scenario == "owner") pack.hid = defenderSideHero->id;
			if(scenario == "missing_hero") pack.hid = ObjectInstanceID::NONE;
			onAuthority([this, scenario]
			{
				if(scenario == "inactive")
				{
					PlayerEndsTurn turn; turn.player = PlayerColor(0);
					authority->gh->sendAndApply(turn);
				}
				if(scenario == "query") authority->gh->queries->addQuery(std::make_shared<BlockingFormationQuery>(authority->gh.get()));
				if(scenario == "connection") authority->permitConnection = false;
			});
			drain(); responses.clear();
			if(typed)
				ASSERT_EQ(clientConnection->trySendExperimentalSetFormation(pack), ExperimentalSetFormationResult::QUEUED);
			else
				clientConnection->sendPack(pack);
			drain();
			EXPECT_EQ(attackerSideHero->formation, scenario == "accepted" ? target : initial);
			EXPECT_EQ(std::count_if(responses.begin(), responses.end(), [](const auto & r) { return r.kind == "formation"; }), scenario == "accepted" ? 1 : 0);
			ASSERT_FALSE(responses.empty());
			EXPECT_EQ(responses.front().kind, "received");
			EXPECT_EQ(responses.back().kind, "applied");
			// Existing behavior: visitor rejection still yields ACK true; query blocking false.
			EXPECT_EQ(responses.back().result, scenario != "query");
			if(typed) EXPECT_EQ(responses, baseline);
			else baseline = responses;
		}
	}
}

TEST_F(ExperimentalSetFormationTest, MixedByteTypedByteUsesAuthorityFifoAndByteResponses)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ASSERT_NO_FATAL_FAILURE(startScenario());
	clientConnection->sendPack(request(1, EArmyFormation::TIGHT));
	ASSERT_EQ(clientConnection->trySendExperimentalSetFormation(request(2, EArmyFormation::LOOSE)), ExperimentalSetFormationResult::QUEUED);
	clientConnection->sendPack(request(3, EArmyFormation::TIGHT));
	drain();
	const auto hero = attackerSideHero->id;
	const std::vector<ObservedResponse> expected = {
		{"received", 1}, {"formation", 0, false, hero, EArmyFormation::TIGHT}, {"applied", 1, true},
		{"received", 2}, {"formation", 0, false, hero, EArmyFormation::LOOSE}, {"applied", 2, true},
		{"received", 3}, {"formation", 0, false, hero, EArmyFormation::TIGHT}, {"applied", 3, true},
	};
	EXPECT_EQ(responses, expected);
	EXPECT_EQ(attackerSideHero->formation, EArmyFormation::TIGHT);
	ASSERT_EQ(authority->callbacks.size(), 3u);
	for(auto thread : authority->callbacks)
	{
		EXPECT_EQ(thread, authority->authorityThread);
		EXPECT_NE(thread, std::this_thread::get_id());
	}
}

TEST_F(ExperimentalSetFormationTest, ReceiverCloseCancelsPendingWithoutFallback)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ASSERT_NO_FATAL_FAILURE(startScenario());
	const auto initial = attackerSideHero->formation;
	ExperimentalSetFormationResult queued, afterClose;
	{
		ExecutorPause pause(authority->getNetworkHandler().getContext());
		queued = clientConnection->trySendExperimentalSetFormation(request(1, EArmyFormation::TIGHT));
		serverEndpoint->close(); // No receiver callback is executing while paused.
		afterClose = clientConnection->trySendExperimentalSetFormation(request(2, EArmyFormation::TIGHT));
	}
	drain();
	EXPECT_EQ(queued, ExperimentalSetFormationResult::QUEUED);
	EXPECT_EQ(afterClose, ExperimentalSetFormationResult::CLOSED);
	EXPECT_TRUE(responses.empty());
	EXPECT_TRUE(authority->callbacks.empty());
	EXPECT_EQ(attackerSideHero->formation, initial);
}

TEST_F(ExperimentalSetFormationTest, SenderCloseAfterEnqueuePreservesPriorRequestOrder)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ExperimentalSetFormationResult queued, afterClose;
	{
		ExecutorPause pause(authority->getNetworkHandler().getContext());
		queued = clientConnection->trySendExperimentalSetFormation(request(1, EArmyFormation::TIGHT));
		clientEndpoint->close(); // Receiver disconnect is queued AFTER the accepted request.
		afterClose = clientConnection->trySendExperimentalSetFormation(request(2, EArmyFormation::LOOSE));
		// Bound the test even if lobby disconnection policy changes: this stop is
		// queued after both the accepted request and the disconnect notification.
		boost::asio::post(authority->getNetworkHandler().getContext(), [this] { authority->stop(); });
	}
	// Disconnect may stop the real server; join instead of posting a later barrier.
	worker.join();
	clientNetwork->getContext().restart(); clientNetwork->getContext().poll();
	EXPECT_EQ(queued, ExperimentalSetFormationResult::QUEUED);
	EXPECT_EQ(afterClose, ExperimentalSetFormationResult::CLOSED);
	EXPECT_EQ(authority->callbacks.size(), 1u);
	EXPECT_EQ(attackerSideHero->formation, EArmyFormation::TIGHT);
	EXPECT_TRUE(responses.empty()); // Closed client drops replies, not a signal to retry bytes.
}

TEST_F(ExperimentalSetFormationTest, ShutdownDropsQueuedRequestAndExpiresReceiverLease)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ASSERT_NO_FATAL_FAILURE(startScenario());
	const auto initial = attackerSideHero->formation;
	ExperimentalSetFormationResult queued;
	{
		ExecutorPause pause(authority->getNetworkHandler().getContext());
		queued = clientConnection->trySendExperimentalSetFormation(request(1, EArmyFormation::TIGHT));
		authority->stop();
	}
	worker.join();
	EXPECT_EQ(queued, ExperimentalSetFormationResult::QUEUED);
	EXPECT_TRUE(authority->callbacks.empty());
	authority.reset(); // Endpoint remains alive; its receiver lease/context no longer does.
	EXPECT_EQ(clientConnection->trySendExperimentalSetFormation(request(2, EArmyFormation::TIGHT)), ExperimentalSetFormationResult::CLOSED);
	EXPECT_EQ(attackerSideHero->formation, initial);
}

TEST_F(ExperimentalSetFormationTest, UnknownEndpointCannotBypassConnectionLookup)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ASSERT_NO_FATAL_FAILURE(startScenario());
	const auto initial = attackerSideHero->formation;
	auto pack = request(1, EArmyFormation::TIGHT);
	EXPECT_THROW(onAuthority([this, pack]() mutable
	{
		// Client endpoint is deliberately NOT the registered server endpoint.
		authority->onExperimentalSetFormation(clientEndpoint, pack);
	}), std::out_of_range);
	EXPECT_TRUE(responses.empty());
	EXPECT_EQ(attackerSideHero->formation, initial);
}

TEST_F(ExperimentalSetFormationTest, NonGameplayGuardIsAnExplicitExperimentalRestriction)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ASSERT_NO_FATAL_FAILURE(startScenario());
	onAuthority([this] { authority->setState(EServerState::LOBBY); });
	const auto initial = attackerSideHero->formation;
	EXPECT_EQ(clientConnection->trySendExperimentalSetFormation(request(1, EArmyFormation::TIGHT)), ExperimentalSetFormationResult::QUEUED);
	drain();
	EXPECT_TRUE(responses.empty());
	EXPECT_EQ(attackerSideHero->formation, initial);
	// Unlike the byte visitor's gh-only check, this lane deliberately drops outside GAMEPLAY.
}

namespace
{
struct PlainClient : INetworkClientListener
{
	NetworkConnectionPtr endpoint;
	void onConnectionEstablished(const NetworkConnectionPtr & connection) override { endpoint = connection; }
	void onConnectionFailed(const std::string & error) override { throw std::runtime_error(error); }
	void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> &) override {}
	void onDisconnected(const NetworkConnectionPtr &, const std::string &) override {}
};
struct PlainServer : INetworkServerListener
{
	unsigned bytes = 0;
	void onNewConnection(const NetworkConnectionPtr &) override {}
	void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> &) override { ++bytes; }
	void onDisconnected(const NetworkConnectionPtr &, const std::string &) override {}
};
struct ReenteringReceiver : PlainServer, ExperimentalSetFormationReceiver
{
	GameConnection * sender = nullptr;
	unsigned calls = 0, depth = 0, maxDepth = 0;
	ExperimentalSetFormationResult nested = ExperimentalSetFormationResult::NOT_ELIGIBLE;
	void onExperimentalSetFormation(const NetworkConnectionPtr &, SetFormation & pack) override
	{
		++depth;
		maxDepth = std::max(maxDepth, depth);
		if(++calls == 1)
			nested = sender->trySendExperimentalSetFormation(pack);
		--depth;
	}
};
struct ThrowingReceiver : PlainServer, ExperimentalSetFormationReceiver
{
	unsigned calls = 0;
	void onExperimentalSetFormation(const NetworkConnectionPtr &, SetFormation &) override
	{
		++calls;
		throw std::runtime_error("Injected receiver failure");
	}
};
}

TEST(ExperimentalSetFormationTransportTest, AbsentCapabilityPermitsOneByteFallback)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	PlainServer receiver; PlainClient client;
	auto serverNetwork = INetworkHandler::createHandler();
	auto clientNetwork = INetworkHandler::createHandler();
	auto server = serverNetwork->createServerTCP(receiver);
	clientNetwork->createInternalConnection(client, *server); clientNetwork->getContext().poll();
	GameConnection connection(client.endpoint);
	SetFormation request;
	EXPECT_EQ(connection.trySendExperimentalSetFormation(request), ExperimentalSetFormationResult::NOT_ELIGIBLE);
	connection.sendPack(request); // The only eligible fallback path.
	serverNetwork->getContext().poll();
	EXPECT_EQ(receiver.bytes, 1u);
	client.endpoint->close();
	EXPECT_EQ(connection.trySendExperimentalSetFormation(request), ExperimentalSetFormationResult::CLOSED);
}

TEST(ExperimentalSetFormationTransportTest, SubmissionOnReceiverExecutorStillDoesNotReenter)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ReenteringReceiver receiver; PlainClient client;
	auto serverNetwork = INetworkHandler::createHandler();
	auto clientNetwork = INetworkHandler::createHandler();
	auto server = serverNetwork->createServerTCP(receiver);
	clientNetwork->createInternalConnection(client, *server); clientNetwork->getContext().poll();
	GameConnection connection(client.endpoint);
	receiver.sender = &connection;
	EXPECT_EQ(connection.trySendExperimentalSetFormation(SetFormation()), ExperimentalSetFormationResult::QUEUED);
	EXPECT_EQ(receiver.calls, 0u);
	serverNetwork->getContext().poll();
	EXPECT_EQ(receiver.nested, ExperimentalSetFormationResult::QUEUED);
	EXPECT_EQ(receiver.calls, 2u);
	EXPECT_EQ(receiver.maxDepth, 1u);
	EXPECT_EQ(receiver.bytes, 0u);
}

TEST(ExperimentalSetFormationTransportTest, LaterReceiverExceptionNeverRequestsByteRetry)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ThrowingReceiver receiver; PlainClient client;
	auto serverNetwork = INetworkHandler::createHandler();
	auto clientNetwork = INetworkHandler::createHandler();
	auto server = serverNetwork->createServerTCP(receiver);
	clientNetwork->createInternalConnection(client, *server); clientNetwork->getContext().poll();
	GameConnection connection(client.endpoint);
	EXPECT_EQ(connection.trySendExperimentalSetFormation(SetFormation()), ExperimentalSetFormationResult::QUEUED);
	EXPECT_EQ(receiver.calls, 0u); // No synchronous receiver invocation.
	EXPECT_THROW(serverNetwork->getContext().poll(), std::runtime_error);
	EXPECT_EQ(receiver.calls, 1u);
	EXPECT_EQ(receiver.bytes, 0u);
}

TEST(ExperimentalSetFormationTransportTest, DestroyedReceiverLeaseDropsPendingEvenIfContextIsPumped)
{
	if(!formationExperimentEnabled()) GTEST_SKIP();
	ThrowingReceiver receiver; PlainClient client;
	auto serverNetwork = INetworkHandler::createHandler();
	auto clientNetwork = INetworkHandler::createHandler();
	auto server = serverNetwork->createServerTCP(receiver);
	clientNetwork->createInternalConnection(client, *server); clientNetwork->getContext().poll();
	GameConnection connection(client.endpoint);
	EXPECT_EQ(connection.trySendExperimentalSetFormation(SetFormation()), ExperimentalSetFormationResult::QUEUED);
	server.reset(); // No executor callback active; invalidate the lease before polling.
	EXPECT_EQ(connection.trySendExperimentalSetFormation(SetFormation()), ExperimentalSetFormationResult::CLOSED);
	EXPECT_NO_THROW(serverNetwork->getContext().poll());
	EXPECT_EQ(receiver.calls, 0u);
	EXPECT_EQ(connection.trySendExperimentalSetFormation(SetFormation()), ExperimentalSetFormationResult::CLOSED);
}

#endif // NH_PERF_EXPERIMENTS
