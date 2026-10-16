/*
 * PacketUpdatePublicationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../server/CVCMIServer.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/network/NetworkInterface.h"
#include "../../lib/serializer/GameConnection.h"

#include <array>
#include <future>
#include <limits>
#include <thread>
#include <utility>

namespace
{
// Two ordinary maintained fixtures, not a serialized live-battle descriptor.
class PublicationWorld final : public HeroCommandFixture
{
public:
	CStack * first = nullptr;
	CStack * second = nullptr;
	void TestBody() override {}
	const std::shared_ptr<CGameState> & state() const { return gameState(); }

	void initialize()
	{
		TinyMapGameTest::SetUp();
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		second = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		ASSERT_FALSE(first->defended());
		ASSERT_FALSE(second->defended());
		ASSERT_EQ(second->guardianSpiritHitPoints, 0);
	}

	JsonNode snapshot() const
	{
		JsonNode result;
		for(auto * unit : {first, second})
		{
			JsonNode row;
			row["id"] = JsonNode(static_cast<int64_t>(unit->unitId()));
			row["defending"] = JsonNode(unit->defended());
			row["guardianHitPoints"] = JsonNode(unit->guardianSpiritHitPoints);
			row["position"] = JsonNode(unit->getPosition().toInt());
			try { row["saved"] = unit->save(); }
			catch(const std::exception & error) { row["saveError"] = JsonNode(error.what()); }
			result.Vector().push_back(std::move(row));
		}
		return result;
	}

	void release()
	{
		server.gameState.reset();
		BattleTestFixture::TearDown();
	}
};

std::string fullHex(const std::vector<std::byte> & bytes)
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(bytes.size() * 2);
	for(auto byte : bytes)
	{
		const auto value = std::to_integer<unsigned>(byte);
		result.push_back(digits[value >> 4]);
		result.push_back(digits[value & 15]);
	}
	return result;
}

struct PublicationReceipt
{
	std::vector<std::byte> bytes;
	JsonNode packetRows;
	JsonNode after;
	std::string error;
};
}

// BEFORE characterization: real CVCMIServer -> GameConnection -> InternalConnection
// -> independent GameConnection receiver -> independent CGameState::apply.
class PacketUpdatePublicationTest : public ::testing::Test, public INetworkClientListener
{
protected:
	PublicationWorld authorityWorld, receiverWorld;
	std::unique_ptr<CVCMIServer> authority;
	std::unique_ptr<INetworkHandler> clientNetwork;
	NetworkConnectionPtr clientEndpoint;
	std::unique_ptr<GameConnection> clientConnection;
	std::thread worker;
	std::exception_ptr workerFailure;
	std::thread::id authorityThread;
	std::vector<PublicationReceipt> receipts;
	JsonNode authorityBefore, receiverBefore;
	bool worldsReady = false;
	std::string authorityResult;

	void onConnectionEstablished(const NetworkConnectionPtr & endpoint) override
	{
		clientEndpoint = endpoint;
		clientConnection = std::make_unique<GameConnection>(endpoint);
		clientConnection->setCallback(*receiverWorld.state());
	}
	void onConnectionFailed(const std::string & message) override { throw std::runtime_error(message); }
	void onDisconnected(const NetworkConnectionPtr &, const std::string &) override {}
	void onPacketReceived(const NetworkConnectionPtr & endpoint, const std::vector<std::byte> & bytes) override
	{
		PublicationReceipt receipt;
		receipt.bytes = bytes; // Full delivered payload before decode; never a synthetic serialization oracle.
		try
		{
			if(endpoint != clientEndpoint) throw std::runtime_error("Wrong receiver endpoint");
			auto decoded = clientConnection->retrievePack(bytes);
			auto * packet = dynamic_cast<BattleUnitsChanged *>(decoded.get());
			if(!packet) throw std::runtime_error("Unexpected client packet type");
			for(const auto & change : packet->changedStacks)
			{
				JsonNode row;
				row["id"] = JsonNode(static_cast<int64_t>(change.id));
				row["operation"] = JsonNode(static_cast<int>(change.operation));
				row["data"] = change.data;
				receipt.packetRows.Vector().push_back(std::move(row));
			}
			receiverWorld.state()->apply(*packet);
		}
		catch(const std::exception & error) { receipt.error = error.what(); }
		receipt.after = receiverWorld.snapshot();
		receipts.push_back(std::move(receipt));
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
		onAuthority([] {}); // Exact maintained FIFO barrier before polling the receiver.
		clientNetwork->getContext().restart();
		clientNetwork->getContext().poll();
	}
	void stopScenario()
	{
		if(authority) authority->stop();
		if(worker.joinable()) worker.join();
		authority.reset();
		clientConnection.reset();
		clientEndpoint.reset();
		clientNetwork.reset();
		if(workerFailure)
		{
			auto error = std::exchange(workerFailure, {});
			std::rethrow_exception(error);
		}
	}
	void SetUp() override
	{
		ASSERT_NO_FATAL_FAILURE(authorityWorld.initialize());
		ASSERT_NO_FATAL_FAILURE(receiverWorld.initialize());
		ASSERT_NE(authorityWorld.state().get(), receiverWorld.state().get());
		ASSERT_NE(authorityWorld.first, receiverWorld.first);
		authorityBefore = authorityWorld.snapshot();
		receiverBefore = receiverWorld.snapshot();
		ASSERT_EQ(authorityBefore, receiverBefore);
		worldsReady = true;
		authorityWorld.gameHandler.reset();
		authority = std::make_unique<CVCMIServer>(0, true);
		authority->prepare(false, false); // Internal-only: no TCP listen or UDP discovery.
		clientNetwork = INetworkHandler::createHandler();
		clientNetwork->createInternalConnection(*this, authority->getNetworkServer());
		clientNetwork->getContext().poll();
		ASSERT_NE(clientConnection, nullptr);
		ASSERT_EQ(authority->activeConnections.size(), 1u);
		authority->activeConnections.front()->connectionID = GameConnectionID::FIRST_CONNECTION;
		authority->gh = std::make_shared<CGameHandler>(*authority, authorityWorld.state());
		worker = std::thread([this]
		{
			authorityThread = std::this_thread::get_id();
			try { authority->run(); }
			catch(...) { workerFailure = std::current_exception(); }
		});
		onAuthority([this] { authority->setState(EServerState::GAMEPLAY); });
		drain();
		ASSERT_TRUE(receipts.empty());
	}
	void TearDown() override
	{
		try { stopScenario(); }
		catch(const std::exception & error) { ADD_FAILURE() << error.what(); }
		catch(...) { ADD_FAILURE() << "Non-standard authoritative worker exception"; }
		// Even fatal assertions preserve delivered frames, after the worker is joined.
		if(worldsReady) record(authorityResult);
		authorityWorld.release();
		receiverWorld.release();
	}
	BattleUnitsChanged pair(bool defending = true) const
	{
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		for(auto * unit : {authorityWorld.first, authorityWorld.second})
		{
			packet.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
			packet.changedStacks.back().data = unit->save();
			packet.changedStacks.back().data["state"]["defending"].Bool() = defending;
		}
		return packet;
	}
	std::string publish(BattleUnitsChanged & packet)
	{
		// Own the request across any deadline failure until the executor is joined.
		auto pending = std::make_shared<BattleUnitsChanged>(packet);
		std::string result;
		try { onAuthority([this, pending] { authority->gh->sendAndApply(*pending); }); }
		catch(const std::runtime_error & error) { result = error.what(); }
		authorityResult = result;
		drain();
		EXPECT_NE(authorityThread, std::this_thread::get_id());
		return result;
	}
	void record(const std::string & authorityError)
	{
		JsonNode result;
		result["authorityError"] = JsonNode(authorityError);
		result["authorityBefore"] = authorityBefore;
		result["receiverBefore"] = receiverBefore;
		result["authorityAfter"] = authorityWorld.snapshot();
		result["receiverAfter"] = receiverWorld.snapshot();
		for(const auto & receipt : receipts)
		{
			JsonNode row;
			row["wireHex"] = JsonNode(fullHex(receipt.bytes));
			row["decodedRows"] = receipt.packetRows;
			row["receiverError"] = JsonNode(receipt.error);
			row["receiverAfter"] = receipt.after;
			result["deliveredFrames"].Vector().push_back(std::move(row));
		}
		RecordProperty("wireCharacterization", result.toCompactString());
	}
};

TEST_F(PacketUpdatePublicationTest, ValidPairsUseCompleteWireFramesInOrderAndIndependentReceiver)
{
	auto first = pair();
	const std::array<JsonNode, 2> requested{first.changedStacks[0].data, first.changedStacks[1].data};
	ASSERT_EQ(publish(first), "");
	auto second = pair(false);
	ASSERT_EQ(publish(second), "");
	record("");
	ASSERT_EQ(receipts.size(), 2u);
	EXPECT_FALSE(receipts[0].bytes.empty());
	EXPECT_FALSE(receipts[1].bytes.empty());
	EXPECT_NE(receipts[0].bytes, receipts[1].bytes);
	for(const auto & receipt : receipts)
	{
		EXPECT_TRUE(receipt.error.empty());
		ASSERT_EQ(receipt.packetRows.Vector().size(), 2u);
		EXPECT_EQ(receipt.packetRows.Vector()[0]["id"].Integer(), authorityWorld.first->unitId());
		EXPECT_EQ(receipt.packetRows.Vector()[1]["id"].Integer(), authorityWorld.second->unitId());
		for(const auto & row : receipt.packetRows.Vector())
			EXPECT_EQ(row["operation"].Integer(), static_cast<int>(UnitChanges::EOperation::UPDATE));
	}
	EXPECT_EQ(receipts[0].after.Vector()[0]["saved"], requested[0]);
	EXPECT_EQ(receipts[0].after.Vector()[1]["saved"], requested[1]);
	EXPECT_EQ(receipts[1].after, receiverBefore);
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
}

TEST_F(PacketUpdatePublicationTest, MissingLaterIdPublishesFrameAndBothWorldsRetainAppliedPrefix)
{
	auto packet = pair();
	packet.changedStacks[1].id = std::numeric_limits<uint32_t>::max();
	const auto requestedFirst = packet.changedStacks[0].data;
	const auto error = publish(packet);
	record(error);
	EXPECT_EQ(error, "Invalid unit id in BattleInfo update");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_FALSE(receipts.front().bytes.empty());
	EXPECT_EQ(receipts.front().error, error);
	ASSERT_EQ(receipts.front().packetRows.Vector().size(), 2u);
	EXPECT_EQ(receipts.front().packetRows.Vector()[1]["id"].Integer(), packet.changedStacks[1].id);
	EXPECT_EQ(authorityWorld.first->save(), requestedFirst);
	EXPECT_EQ(receiverWorld.first->save(), requestedFirst);
	EXPECT_EQ(authorityWorld.snapshot(), receiverWorld.snapshot());
	EXPECT_EQ(authorityWorld.second->save(), authorityBefore.Vector()[1]["saved"]);
}

TEST_F(PacketUpdatePublicationTest, NegativeLaterGuardianPublishesFrameAndExposesReceiverPartialMutation)
{
	auto packet = pair();
	packet.changedStacks[1].data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	const auto requestedFirst = packet.changedStacks[0].data;
	const auto error = publish(packet);
	record(error);
	EXPECT_EQ(error, "Invalid Guardian Spirit state");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_FALSE(receipts.front().bytes.empty());
	EXPECT_EQ(receipts.front().error, error);
	EXPECT_EQ(authorityWorld.first->save(), requestedFirst);
	EXPECT_EQ(receiverWorld.first->save(), requestedFirst);
	for(auto * unit : {authorityWorld.second, receiverWorld.second})
	{
		EXPECT_TRUE(unit->defended());
		EXPECT_EQ(unit->guardianSpiritHitPoints, -1);
		EXPECT_EQ(unit->getPosition(), BattleHex::INVALID);
		EXPECT_THROW(unit->save(), std::runtime_error);
	}
	EXPECT_EQ(authorityWorld.snapshot(), receiverWorld.snapshot());
}

TEST_F(PacketUpdatePublicationTest, KnownFrozenPreflightFailurePublishesNoFrameAndPreservesBothWorlds)
{
	auto packet = pair();
	packet.changedStacks[1].data["state"]["frozenAppliedRound"].Integer() = -2;
	const auto error = publish(packet);
	record(error);
	EXPECT_EQ(error, "Invalid Frozen application receipt in unit update");
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
	// No retry through the same writer: failure may leave private serializer staging state.
}
