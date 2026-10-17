/*
 * PreparedRecipientPublicationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/network/NetworkConnection.h"
#include "../../lib/network/PreparedNetworkBatch.h"
#include "../../lib/serializer/GameConnection.h"
#include "../../lib/serializer/PreparedGameConnectionBatch.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/gameState/CGameState.h"
#include <future>
#include <thread>

namespace
{
class Recorder final : public INetworkConnectionListener
{
public:
	std::vector<std::vector<std::byte>> packets;
	size_t disconnected = 0;
	void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> & bytes) override
	{
		packets.push_back(bytes);
	}
	void onDisconnected(const NetworkConnectionPtr &, const std::string &) override
	{
		++disconnected;
	}
};

class ScalarSink final : public INetworkConnection
{
public:
	std::vector<std::vector<std::byte>> packets;
	void sendPacket(const std::vector<std::byte> & bytes) override { packets.push_back(bytes); }
	void close() override {}
	void setAsyncWritesEnabled(bool) override {}
};

BattleUnitsChanged packetWithText(const std::string & text = "repeated dictionary string")
{
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	UnitChanges change(1, UnitChanges::EOperation::UPDATE);
	change.data["state"]["diagnosticText"] = JsonNode(text);
	packet.changedStacks.push_back(std::move(change));
	return packet;
}

class PreparedRecipientPublicationTest : public ::testing::Test
{
protected:
	NetworkContext context;
	Recorder senderListener, receiverListener;
	std::shared_ptr<InternalConnection> sender = std::make_shared<InternalConnection>(senderListener, context);
	std::shared_ptr<InternalConnection> receiver = std::make_shared<InternalConnection>(receiverListener, context);
	std::shared_ptr<GameConnection> connection = std::make_shared<GameConnection>(sender);
	std::vector<std::shared_ptr<GameConnection>> recipients{connection};

	void SetUp() override
	{
		sender->connectTo(receiver);
		receiver->connectTo(sender);
	}

	void publish(const BattleUnitsChanged & packet)
	{
		PreparedGameConnectionBatch batch(context, recipients, packet);
		ASSERT_TRUE(batch.ready());
		ASSERT_TRUE(batch.accept());
		EXPECT_FALSE(batch.ready());
		EXPECT_FALSE(batch.accept());
	}
};

TEST_F(PreparedRecipientPublicationTest, NoFrameUntilAcceptedAndRetired)
{
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packetWithText());
		EXPECT_TRUE(receiverListener.packets.empty());
		ASSERT_TRUE(batch.accept());
		EXPECT_TRUE(receiverListener.packets.empty());
	});
	context.run();
	ASSERT_EQ(receiverListener.packets.size(), 1);
}

TEST_F(PreparedRecipientPublicationTest, DestructionCancelsScheduledFrames)
{
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packetWithText());
		ASSERT_TRUE(batch.ready());
	});
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, CancelledDictionaryDoesNotChangeNextOrdinaryBytes)
{
	auto scalar = std::make_shared<ScalarSink>();
	GameConnection oracle(scalar);
	const auto packet = packetWithText();
	oracle.sendPack(packet);
	boost::asio::post(context, [&]
	{
		{ PreparedGameConnectionBatch abandoned(context, recipients, packet); }
		connection->sendPack(packet);
	});
	context.run();
	EXPECT_EQ(receiverListener.packets, scalar->packets);
}

TEST_F(PreparedRecipientPublicationTest, PreparedThenOrdinaryUsesExactPersistentDictionary)
{
	auto scalar = std::make_shared<ScalarSink>();
	GameConnection oracle(scalar);
	const auto packet = packetWithText();
	oracle.sendPack(packet);
	oracle.sendPack(packet);
	boost::asio::post(context, [&]
	{
		publish(packet);
		connection->sendPack(packet);
	});
	context.run();
	ASSERT_EQ(receiverListener.packets.size(), 2);
	EXPECT_EQ(receiverListener.packets, scalar->packets);
	EXPECT_NE(receiverListener.packets[0], receiverListener.packets[1]);
}

TEST_F(PreparedRecipientPublicationTest, DuplicateRecipientOccurrencesAreNotDeduplicated)
{
	recipients.push_back(connection);
	auto scalar = std::make_shared<ScalarSink>();
	GameConnection oracle(scalar);
	const auto packet = packetWithText();
	oracle.sendPack(packet);
	oracle.sendPack(packet);
	boost::asio::post(context, [&] { publish(packet); });
	context.run();
	ASSERT_EQ(receiverListener.packets.size(), 2);
	EXPECT_EQ(receiverListener.packets, scalar->packets);
}

TEST_F(PreparedRecipientPublicationTest, TwoGameConnectionsCanAliasOneEndpointWithoutLosingFrames)
{
	recipients.push_back(std::make_shared<GameConnection>(sender));
	boost::asio::post(context, [&] { publish(packetWithText()); });
	context.run();
	ASSERT_EQ(receiverListener.packets.size(), 2);
	EXPECT_EQ(receiverListener.packets[0], receiverListener.packets[1]);
}

TEST_F(PreparedRecipientPublicationTest, LaterRecipientVersionRefusalPublishesNothingAndKeepsDictionaries)
{
	auto old = std::make_shared<GameConnection>(sender);
	old->setSerializationVersion(static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_PHOENIX_SPARK) - 1));
	recipients.push_back(old);
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{BattleSide::ATTACKER, 1};
	boost::asio::post(context, [&]
	{
		EXPECT_THROW((void)PreparedGameConnectionBatch(context, recipients, packet), std::runtime_error);
	});
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
	auto scalar = std::make_shared<ScalarSink>();
	GameConnection oracle(scalar);
	const auto ordinary = packetWithText();
	oracle.sendPack(ordinary);
	context.restart();
	boost::asio::post(context, [&] { connection->sendPack(ordinary); });
	context.run();
	EXPECT_EQ(receiverListener.packets, scalar->packets);
}

TEST_F(PreparedRecipientPublicationTest, UnknownTransportRefusesWithoutCallingItsSend)
{
	auto unknown = std::make_shared<ScalarSink>();
	recipients.push_back(std::make_shared<GameConnection>(unknown));
	boost::asio::post(context, [&]
	{
		EXPECT_THROW((void)PreparedGameConnectionBatch(context, recipients, packetWithText()), std::runtime_error);
	});
	context.run();
	EXPECT_TRUE(unknown->packets.empty());
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, NullRecipientRefusesBeforeScheduling)
{
	recipients.push_back({});
	boost::asio::post(context, [&]
	{
		EXPECT_THROW((void)PreparedGameConnectionBatch(context, recipients, packetWithText()), std::runtime_error);
	});
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, ReorderedRecipientOwnerRefusesAcceptance)
{
	auto other = std::make_shared<GameConnection>(sender);
	recipients.push_back(other);
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packetWithText());
		std::swap(recipients[0], recipients[1]);
		EXPECT_FALSE(batch.ready());
		EXPECT_FALSE(batch.accept());
	});
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, ChangedRecipientIdentityRefusesAcceptance)
{
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packetWithText());
		connection->uuid = "changed";
		EXPECT_FALSE(batch.accept());
	});
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, RequiresActualPublisherThread)
{
	EXPECT_THROW((void)PreparedGameConnectionBatch(context, recipients, packetWithText()), std::logic_error);
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, EmptyRecipientSetIsARealEmptyPublication)
{
	recipients.clear();
	const auto packet = packetWithText();
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packet);
		EXPECT_EQ(batch.packet().changedStacks[0].data, packet.changedStacks[0].data);
		EXPECT_TRUE(batch.accept());
	});
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, OwnedPacketRetainsExactWriterProjectionWithoutMutatingCaller)
{
	const auto packet = packetWithText();
	const auto before = packet.changedStacks[0].data;
	auto expected = packet;
	auto scalar = std::make_shared<ScalarSink>();
	GameConnection oracle(scalar);
	oracle.sendPack(expected);
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packet);
		EXPECT_EQ(batch.packet().changedStacks[0].data, expected.changedStacks[0].data);
		EXPECT_EQ(packet.changedStacks[0].data, before);
		ASSERT_TRUE(batch.accept());
	});
	context.run();
	EXPECT_EQ(packet.changedStacks[0].data, before);
}

TEST_F(PreparedRecipientPublicationTest, ClosedInternalPeerRefusesPreparation)
{
	receiver->close();
	boost::asio::post(context, [&]
	{
		EXPECT_THROW((void)PreparedGameConnectionBatch(context, recipients, packetWithText()), std::runtime_error);
	});
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, OrdinaryReceiveAndDisconnectKeepNotificationOrder)
{
	sender->sendPacket({std::byte{1}});
	receiver->disconnect();
	sender->sendPacket({std::byte{2}});
	context.run();
	ASSERT_EQ(receiverListener.packets.size(), 1);
	EXPECT_EQ(receiverListener.packets[0], std::vector<std::byte>{std::byte{1}});
	EXPECT_EQ(receiverListener.disconnected, 1);
}

TEST_F(PreparedRecipientPublicationTest, PostedCancellationOwnsEndpointUntilItsHandlerRetires)
{
	auto localSender = std::make_shared<InternalConnection>(senderListener, context);
	auto localReceiver = std::make_shared<InternalConnection>(receiverListener, context);
	localSender->connectTo(localReceiver);
	localReceiver->connectTo(localSender);
	std::weak_ptr<InternalConnection> weak = localReceiver;
	{
		PreparedNetworkBatch batch({{localSender, {std::byte{1}}}});
		localSender.reset();
		localReceiver.reset();
		EXPECT_FALSE(weak.expired());
	}
	EXPECT_FALSE(weak.expired());
	context.run();
	EXPECT_TRUE(weak.expired());
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, AddUpdateRemoveEncodingUsesTheExistingTypedReader)
{
	auto packet = packetWithText();
	for(auto operation : {UnitChanges::EOperation::ADD, UnitChanges::EOperation::REMOVE})
	{
		auto change = packet.changedStacks.front();
		change.operation = operation;
		packet.changedStacks.push_back(change);
	}
	boost::asio::post(context, [&] { publish(packet); });
	context.run();
	ASSERT_EQ(receiverListener.packets.size(), 1);
	GameConnection decoder(receiver);
	auto decoded = decoder.retrievePack(receiverListener.packets[0]);
	auto * changed = dynamic_cast<BattleUnitsChanged *>(decoded.get());
	ASSERT_NE(changed, nullptr);
	ASSERT_EQ(changed->changedStacks.size(), 3);
	for(size_t i = 0; i < 3; ++i)
	{
		EXPECT_EQ(changed->changedStacks[i].id, packet.changedStacks[i].id);
		EXPECT_EQ(changed->changedStacks[i].operation, packet.changedStacks[i].operation);
		EXPECT_EQ(changed->changedStacks[i].data["state"]["diagnosticText"],
			packet.changedStacks[i].data["state"]["diagnosticText"]);
	}
}

TEST_F(PreparedRecipientPublicationTest, StoppedContextRefusesBeforeFirstWrite)
{
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packetWithText());
		context.stop();
		EXPECT_FALSE(batch.ready());
		EXPECT_FALSE(batch.accept());
	});
	context.run();
	context.restart();
	context.run();
	EXPECT_TRUE(receiverListener.packets.empty());
}

TEST_F(PreparedRecipientPublicationTest, WrongThreadCannotAcceptAndOriginalOwnerCanContinue)
{
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packetWithText());
		std::thread other([&]
		{
			EXPECT_FALSE(batch.ready());
			EXPECT_FALSE(batch.accept());
		});
		other.join();
		ASSERT_TRUE(batch.accept());
	});
	context.run();
	ASSERT_EQ(receiverListener.packets.size(), 1);
}

TEST_F(PreparedRecipientPublicationTest, ListenerWaitingForWorldMutexDoesNotRetainEndpointMutex)
{
	class WorldListener final : public INetworkConnectionListener
	{
	public:
		std::promise<void> entered;
		size_t messages = 0;
		void onPacketReceived(const NetworkConnectionPtr &, const std::vector<std::byte> &) override
		{
			if(messages++ == 0)
			{
				entered.set_value();
				std::unique_lock world(CGameState::mutex);
			}
		}
		void onDisconnected(const NetworkConnectionPtr &, const std::string &) override {}
	} listener;
	auto origin = std::make_shared<InternalConnection>(senderListener, context);
	auto target = std::make_shared<InternalConnection>(listener, context);
	origin->connectTo(target);
	target->connectTo(origin);
	auto entered = listener.entered.get_future();
	std::promise<void> posted;
	auto completed = posted.get_future();
	auto keepRunning = boost::asio::make_work_guard(context);
	std::unique_lock world(CGameState::mutex);
	std::thread worker;
	{
		PreparedNetworkBatch batch({{origin, {std::byte{1}}}});
		worker = std::thread([&] { context.run(); });
		EXPECT_TRUE(batch.accept());
	} // Retire transport locks while deliberately retaining the world lock.
	const auto first = entered.wait_for(std::chrono::seconds(1));
	std::thread later([&]
	{
		origin->sendPacket({std::byte{2}});
		posted.set_value();
	});
	const auto second = completed.wait_for(std::chrono::seconds(1));
	// Always unblock/join before reporting, including the former ABBA shape.
	world.unlock();
	later.join();
	keepRunning.reset();
	worker.join();
	EXPECT_EQ(first, std::future_status::ready);
	EXPECT_EQ(second, std::future_status::ready);
	EXPECT_EQ(listener.messages, 2);
}

class PreparedTcpPublicationTest : public ::testing::Test
{
protected:
	NetworkContext context;
	Recorder listener;
	boost::asio::ip::tcp::acceptor acceptor{context, {boost::asio::ip::address_v4::loopback(), 0}};
	NetworkSocket peer{context};
	std::shared_ptr<NetworkSocket> socket = std::make_shared<NetworkSocket>(context);
	std::shared_ptr<NetworkConnection> endpoint;

	void SetUp() override
	{
		peer.connect(acceptor.local_endpoint());
		acceptor.accept(*socket);
		endpoint = std::make_shared<NetworkConnection>(listener, socket, context);
	}

	std::vector<std::byte> receive()
	{
		if(peer.available() < sizeof(uint32_t))
		{
			ADD_FAILURE() << "Missing complete prepared frame header";
			return {};
		}
		uint32_t size = 0;
		boost::asio::read(peer, boost::asio::buffer(&size, sizeof(size)));
		if(peer.available() < size)
		{
			ADD_FAILURE() << "Missing complete prepared frame payload";
			return {};
		}
		std::vector<std::byte> bytes(size);
		if(size)
			boost::asio::read(peer, boost::asio::buffer(bytes));
		return bytes;
	}

	void ordered(bool asynchronous)
	{
		endpoint->setAsyncWritesEnabled(asynchronous);
		{
			PreparedNetworkBatch batch({{endpoint, {std::byte{1}}}, {endpoint, {}}});
			EXPECT_EQ(peer.available(), 0);
			ASSERT_TRUE(batch.accept());
			EXPECT_FALSE(batch.accept());
			EXPECT_EQ(peer.available(), 0);
		}
		endpoint->sendPacket({std::byte{3}});
		if(!asynchronous)
			EXPECT_GT(peer.available(), 0); // Ordinary sync send still writes before returning.
		context.run();
		EXPECT_EQ(receive(), std::vector<std::byte>{std::byte{1}});
		EXPECT_TRUE(receive().empty());
		EXPECT_EQ(receive(), std::vector<std::byte>{std::byte{3}});
	}
};

TEST_F(PreparedTcpPublicationTest, SynchronousQueuePreservesHeaderPayloadAndLaterSendOrder)
{
	ordered(false);
}

TEST_F(PreparedTcpPublicationTest, AsynchronousQueuePreservesHeaderPayloadAndLaterSendOrder)
{
	ordered(true);
}

TEST_F(PreparedTcpPublicationTest, CancelledTcpBatchPublishesNoHeader)
{
	{ PreparedNetworkBatch batch({{endpoint, {std::byte{1}}}}); }
	context.run();
	EXPECT_EQ(peer.available(), 0);
}

TEST_F(PreparedTcpPublicationTest, ClosedTcpRefusesBeforeAcceptance)
{
	endpoint->close();
	EXPECT_THROW((void)PreparedNetworkBatch({{endpoint, {std::byte{1}}}}), std::runtime_error);
	context.run();
	EXPECT_EQ(peer.available(), 0);
}

TEST_F(PreparedTcpPublicationTest, MixedBuiltInRecipientsAcceptOneOwnedBatch)
{
	Recorder internalListener;
	auto internalSender = std::make_shared<InternalConnection>(listener, context);
	auto internalReceiver = std::make_shared<InternalConnection>(internalListener, context);
	internalSender->connectTo(internalReceiver);
	internalReceiver->connectTo(internalSender);
	std::vector<std::shared_ptr<GameConnection>> recipients{
		std::make_shared<GameConnection>(endpoint), std::make_shared<GameConnection>(internalSender)};
	boost::asio::post(context, [&]
	{
		PreparedGameConnectionBatch batch(context, recipients, packetWithText());
		EXPECT_TRUE(internalListener.packets.empty());
		EXPECT_EQ(peer.available(), 0);
		ASSERT_TRUE(batch.accept());
	});
	context.run();
	ASSERT_EQ(internalListener.packets.size(), 1);
	EXPECT_EQ(receive(), internalListener.packets[0]);
}

TEST_F(PreparedTcpPublicationTest, LaterInternalVersionRefusalCannotPublishEarlierTcpPrefix)
{
	Recorder internalListener;
	auto internalSender = std::make_shared<InternalConnection>(listener, context);
	auto internalReceiver = std::make_shared<InternalConnection>(internalListener, context);
	internalSender->connectTo(internalReceiver);
	internalReceiver->connectTo(internalSender);
	auto old = std::make_shared<GameConnection>(internalSender);
	old->setSerializationVersion(static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_PHOENIX_SPARK) - 1));
	std::vector<std::shared_ptr<GameConnection>> recipients{std::make_shared<GameConnection>(endpoint), old};
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.phoenixSparkConsumption = newHorizonsElementalRebirth::PhoenixSparkConsumption{BattleSide::ATTACKER, 1};
	boost::asio::post(context, [&]
	{
		EXPECT_THROW((void)PreparedGameConnectionBatch(context, recipients, packet), std::runtime_error);
	});
	context.run();
	EXPECT_EQ(peer.available(), 0);
	EXPECT_TRUE(internalListener.packets.empty());
}

TEST_F(PreparedTcpPublicationTest, SocketFailureAfterAcceptanceIsADeliveryFailureNotSemanticRefusal)
{
	bool accepted = false;
	{
		PreparedNetworkBatch batch({{endpoint, {std::byte{1}}}});
		accepted = batch.accept();
	}
	ASSERT_TRUE(accepted);
	endpoint->close();
	context.run();
	EXPECT_TRUE(accepted);
	EXPECT_EQ(listener.disconnected, 1);
	EXPECT_EQ(peer.available(), 0);
}

TEST_F(PreparedTcpPublicationTest, SwitchingToSyncDoesNotOverlapAnExistingAsyncWrite)
{
	endpoint->setAsyncWritesEnabled(true);
	endpoint->sendPacket({std::byte{1}});
	{
		PreparedNetworkBatch batch({{endpoint, {std::byte{2}}}});
		ASSERT_TRUE(batch.accept());
	}
	endpoint->setAsyncWritesEnabled(false);
	endpoint->sendPacket({std::byte{3}});
	context.run();
	EXPECT_EQ(receive(), std::vector<std::byte>{std::byte{1}});
	EXPECT_EQ(receive(), std::vector<std::byte>{std::byte{2}});
	EXPECT_EQ(receive(), std::vector<std::byte>{std::byte{3}});
	endpoint->sendPacket({std::byte{4}});
	EXPECT_EQ(receive(), std::vector<std::byte>{std::byte{4}});
}

TEST_F(PreparedTcpPublicationTest, OrdinarySyncDrainReportsFailureAfterReturningFromWriterLocks)
{
	{
		PreparedNetworkBatch batch({{endpoint, {std::byte{1}}}});
		ASSERT_TRUE(batch.accept());
	}
	endpoint->close();
	GameConnection ordinary(endpoint);
	ordinary.sendPack(packetWithText());
	EXPECT_EQ(listener.disconnected, 0);
	context.run();
	EXPECT_EQ(listener.disconnected, 1);
}
}
