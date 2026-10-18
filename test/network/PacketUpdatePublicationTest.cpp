/*
 * PacketUpdatePublicationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../server/CVCMIServer.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/battle/PreparedBattleWorldCommit.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../server/PreparedBattleWorldPublication.h"
#include "../../server/BattleUnitsChangedInput.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include <functional>
#include "../../lib/network/NetworkInterface.h"
#include "../../lib/serializer/GameConnection.h"

#include <array>
#include <future>
#include <limits>
#include <optional>
#include <boost/asio/executor_work_guard.hpp>
#include <thread>
#include <utility>

namespace
{
template<typename Value>
std::vector<std::byte> publicationValueBytes(Value & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

using PublicationBonusRows = std::vector<std::pair<const Bonus *, std::vector<std::byte>>>;

PublicationBonusRows publicationBonusRows(const BonusList & bonuses)
{
	PublicationBonusRows result;
	for(const auto & bonus : bonuses)
	{
		auto value = *bonus;
		result.emplace_back(bonus.get(), publicationValueBytes(value));
	}
	return result;
}

// Two ordinary maintained fixtures, not a serialized live-battle descriptor.
class PublicationWorld final : public HeroCommandFixture
{
public:
	CStack * first = nullptr;
	CStack * second = nullptr;
	void TestBody() override {}
	const std::shared_ptr<CGameState> & state() const { return gameState(); }
	std::vector<std::byte> callbackRandomBytes()
	{
		return publicationValueBytes(*static_cast<CRandomGenerator *>(TinyMapGameTest::getRNG()));
	}

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

// Real CVCMIServer -> GameConnection -> InternalConnection
// -> independent GameConnection receiver -> independent CGameState::apply.
class PacketUpdatePublicationTest : public ::testing::Test, public INetworkClientListener
{
protected:
	PublicationWorld authorityWorld, receiverWorld;
	std::unique_ptr<CVCMIServer> authority;
	std::unique_ptr<INetworkHandler> clientNetwork;
	std::optional<boost::asio::executor_work_guard<NetworkContext::executor_type>> receiverWork;
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
		EXPECT_FALSE(clientNetwork->getContext().stopped());
	}
	void stopScenario()
	{
		if(authority) authority->stop();
		if(worker.joinable()) worker.join();
		receiverWork.reset(); // Work must retire before its receiver context is destroyed.
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
		receiverWork.emplace(clientNetwork->getContext().get_executor());
		clientNetwork->createInternalConnection(*this, authority->getNetworkServer());
		clientNetwork->getContext().poll();
		ASSERT_NE(clientConnection, nullptr);
		ASSERT_FALSE(clientNetwork->getContext().stopped());
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
		if(!result.empty())
		{
			EXPECT_TRUE(sameBattleUnitsChangedInput(*pending, packet));
		}
		return result;
	}
	void expectOrdinaryRejection(BattleUnitsChanged & packet, const std::string & expected)
	{
		std::vector<std::byte> replay, random;
		onAuthority([this, &replay, &random]
		{
			replay = publicationValueBytes(authorityWorld.state()->replayLog);
			random = publicationValueBytes(*authority->gh->randomizer);
		});
		const auto authorityCallback = authorityWorld.callbackRandomBytes();
		const auto receiverCallback = receiverWorld.callbackRandomBytes();
		const auto receiverReplay = publicationValueBytes(receiverWorld.state()->replayLog);
		const std::array<CStack *, 4> units{authorityWorld.first, authorityWorld.second,
			receiverWorld.first, receiverWorld.second};
		std::array<TConstBonusListPtr, 4> retained;
		std::array<PublicationBonusRows, 4> local, query;
		for(size_t i = 0; i < units.size(); ++i)
		{
			local[i] = publicationBonusRows(units[i]->getExportedBonusList());
			retained[i] = units[i]->getBonuses(Selector::type()(BonusType::STACKS_SPEED));
			query[i] = publicationBonusRows(*retained[i]);
		}
		EXPECT_EQ(publish(packet), expected);
		EXPECT_TRUE(receipts.empty());
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
		onAuthority([this, &replay, &random]
		{
			EXPECT_EQ(publicationValueBytes(authorityWorld.state()->replayLog), replay);
			EXPECT_EQ(publicationValueBytes(*authority->gh->randomizer), random);
		});
		EXPECT_EQ(authorityWorld.callbackRandomBytes(), authorityCallback);
		EXPECT_EQ(receiverWorld.callbackRandomBytes(), receiverCallback);
		EXPECT_EQ(publicationValueBytes(receiverWorld.state()->replayLog), receiverReplay);
		for(size_t i = 0; i < units.size(); ++i)
		{
			EXPECT_EQ(publicationBonusRows(units[i]->getExportedBonusList()), local[i]);
			EXPECT_EQ(publicationBonusRows(*retained[i]), query[i]);
			EXPECT_EQ(publicationBonusRows(*units[i]->getBonuses(Selector::type()(BonusType::STACKS_SPEED))), query[i]);
		}
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

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteRejectsMissingLaterIdBeforeFramesWorldOrReplay)
{
	auto packet = pair();
	packet.changedStacks[1].id = std::numeric_limits<uint32_t>::max();
	expectOrdinaryRejection(packet, "Invalid unit id in BattleInfo update");
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteRejectsNegativeLaterGuardianBeforeFramesWorldOrReplay)
{
	auto packet = pair();
	packet.changedStacks[1].data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	expectOrdinaryRejection(packet, "Invalid Guardian Spirit state");
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteRejectsBeforeOpeningAnActiveReplayChapter)
{
	onAuthority([this]
	{
		// An empty active log requests a full snapshot when apply is entered.
		// No ongoing-battle serialization workaround may mask this ordering.
		authorityWorld.state()->replayLog = ReplayLog();
		authorityWorld.state()->replayLog.configure(false, 2);
	});
	auto packet = pair();
	packet.changedStacks[1].id = std::numeric_limits<uint32_t>::max();
	expectOrdinaryRejection(packet, "Invalid unit id in BattleInfo update");
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteRejectsUnknownLaterOperationBeforeFramesWorldOrReplay)
{
	auto packet = pair();
	packet.changedStacks[1].operation = static_cast<UnitChanges::EOperation>(99);
	expectOrdinaryRejection(packet, "Unknown unit operation");
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteRejectsMissingBattleBeforeFramesWorldOrReplay)
{
	auto packet = pair();
	packet.battleID = BattleID(999999);
	expectOrdinaryRejection(packet, "BattleUnitsChanged references a missing battle");
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRoutePreservesRepeatedIdEntryOrderAndPublishesOneFrame)
{
	auto packet = pair();
	packet.changedStacks[1] = packet.changedStacks.front();
	packet.changedStacks[1].data["state"]["defending"].Bool() = false;
	std::vector<std::byte> random;
	onAuthority([this, &random] { random = publicationValueBytes(*authority->gh->randomizer); });
	const auto callbackRandom = authorityWorld.callbackRandomBytes();
	ASSERT_EQ(publish(packet), "");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	ASSERT_EQ(receipts.front().packetRows.Vector().size(), 2u);
	EXPECT_TRUE(receipts.front().packetRows.Vector()[0]["data"]["state"]["defending"].Bool());
	EXPECT_FALSE(receipts.front().packetRows.Vector()[1]["data"]["state"]["defending"].Bool());
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
	onAuthority([this, &random]
	{
		EXPECT_EQ(publicationValueBytes(*authority->gh->randomizer), random);
	});
	EXPECT_EQ(authorityWorld.callbackRandomBytes(), callbackRandom);
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteStillIgnoresStoppedMalformedEntryBeforePayloadParsing)
{
	onAuthority([this]
	{
		authorityWorld.second->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	});
	receiverWorld.second->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	auto packet = pair();
	packet.changedStacks[1].data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	const auto stopped = authorityWorld.second->save();
	ASSERT_EQ(publish(packet), "");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	EXPECT_TRUE(authorityWorld.first->defended());
	EXPECT_TRUE(receiverWorld.first->defended());
	EXPECT_EQ(authorityWorld.second->save(), stopped);
	EXPECT_EQ(receiverWorld.second->save(), stopped);
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteAddsThenRemovesThroughTheIndependentReceiver)
{
	auto * authorityBattle = authorityWorld.state()->getBattle(BattleID(0));
	auto * receiverBattle = receiverWorld.state()->getBattle(BattleID(0));
	const auto size = authorityBattle->stacks.size();
	battle::UnitInfo info;
	info.id = authorityBattle->battleNextUnitId();
	info.type = authorityWorld.first->creatureId();
	info.count = 2;
	info.side = BattleSide::ATTACKER;
	info.position = BattleHex(5, 5);
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	info.save(packet.changedStacks.back().data);
	ASSERT_EQ(publish(packet), "");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	ASSERT_EQ(authorityBattle->stacks.size(), size + 1);
	ASSERT_EQ(receiverBattle->stacks.size(), size + 1);
	auto * added = authorityBattle->stacks.back().get();
	auto * received = receiverBattle->stacks.back().get();
	EXPECT_NE(added, received);
	EXPECT_TRUE(added->alive());
	EXPECT_EQ(added->getCount(), 2);
	EXPECT_EQ(added->getPosition(), info.position);
	EXPECT_EQ(added->save(), received->save());
	packet.changedStacks.clear();
	packet.changedStacks.emplace_back(info.id, UnitChanges::EOperation::REMOVE);
	ASSERT_EQ(publish(packet), "");
	ASSERT_EQ(receipts.size(), 2u);
	EXPECT_TRUE(receipts.back().error.empty());
	EXPECT_EQ(authorityBattle->stacks.back().get(), added);
	EXPECT_EQ(receiverBattle->stacks.back().get(), received);
	EXPECT_TRUE(added->isGhost());
	EXPECT_TRUE(received->isGhost());
	EXPECT_EQ(added->save(), received->save());
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteDeathAndRevivalKeepNativeRootsAndReceiverState)
{
	const auto living = authorityWorld.first->save();
	const auto health = authorityWorld.first->getAvailableHealth();
	auto dead = authorityWorld.first->acquireState();
	auto damage = health;
	dead->damage(damage);
	const auto deadJson = dead->save();
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.emplace_back(authorityWorld.first->unitId(), UnitChanges::EOperation::UPDATE);
	packet.changedStacks.back().healthDelta = -health;
	packet.changedStacks.back().data = deadJson;
	std::vector<std::byte> random;
	onAuthority([this, &random] { random = publicationValueBytes(*authority->gh->randomizer); });
	const auto callbackRandom = authorityWorld.callbackRandomBytes();
	ASSERT_EQ(publish(packet), "");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	EXPECT_FALSE(authorityWorld.first->alive());
	EXPECT_FALSE(receiverWorld.first->alive());
	EXPECT_EQ(receipts.front().after.Vector()[0]["saved"], deadJson);
	packet.changedStacks.back().healthDelta = health;
	packet.changedStacks.back().data = living;
	ASSERT_EQ(publish(packet), "");
	ASSERT_EQ(receipts.size(), 2u);
	EXPECT_TRUE(receipts.back().error.empty());
	EXPECT_TRUE(authorityWorld.first->alive());
	EXPECT_TRUE(receiverWorld.first->alive());
	EXPECT_EQ(authorityWorld.state()->getBattle(BattleID(0))->getStack(authorityWorld.first->unitId(), false), authorityWorld.first);
	EXPECT_EQ(receiverWorld.state()->getBattle(BattleID(0))->getStack(receiverWorld.first->unitId(), false), receiverWorld.first);
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
	onAuthority([this, &random]
	{
		EXPECT_EQ(publicationValueBytes(*authority->gh->randomizer), random);
	});
	EXPECT_EQ(authorityWorld.callbackRandomBytes(), callbackRandom);
}

TEST_F(PacketUpdatePublicationTest, OrdinaryRouteOccupiedRevivalIgnoresMalformedPayloadAfterOrderedAdd)
{
	const auto position = authorityWorld.first->getPosition();
	const auto health = authorityWorld.first->getAvailableHealth();
	auto dead = authorityWorld.first->acquireState();
	auto damage = health;
	dead->damage(damage);
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.emplace_back(authorityWorld.first->unitId(), UnitChanges::EOperation::UPDATE);
	packet.changedStacks.back().healthDelta = -health;
	packet.changedStacks.back().data = dead->save();
	battle::UnitInfo info;
	info.id = authorityWorld.state()->getBattle(BattleID(0))->battleNextUnitId();
	info.type = authorityWorld.second->creatureId();
	info.count = 1;
	info.side = BattleSide::DEFENDER;
	info.position = position;
	packet.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
	info.save(packet.changedStacks.back().data);
	packet.changedStacks.emplace_back(authorityWorld.first->unitId(), UnitChanges::EOperation::UPDATE);
	packet.changedStacks.back().healthDelta = health;
	packet.changedStacks.back().data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	ASSERT_EQ(publish(packet), "");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	EXPECT_FALSE(authorityWorld.first->alive());
	EXPECT_FALSE(receiverWorld.first->alive());
	EXPECT_EQ(authorityWorld.first->save(), dead->save());
	EXPECT_EQ(receiverWorld.first->save(), dead->save());
	auto * added = authorityWorld.state()->getBattle(BattleID(0))->stacks.back().get();
	auto * received = receiverWorld.state()->getBattle(BattleID(0))->stacks.back().get();
	EXPECT_EQ(added->unitId(), info.id);
	EXPECT_EQ(added->getPosition(), position);
	EXPECT_EQ(added->save(), received->save());
}

TEST_F(PacketUpdatePublicationTest, KnownFrozenStatePreflightFailurePublishesNoFrameAndPreservesBothWorlds)
{
	auto packet = pair();
	packet.changedStacks[1].data["state"]["frozenAppliedRound"].Integer() = -2;
	const auto error = publish(packet);
	record(error);
	EXPECT_EQ(error, "Invalid Frozen application round");
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
	// The state decoder now rejects before any recipient writer is entered.
}

TEST_F(PacketUpdatePublicationTest, NativeGraphAdmissionCoversActualAuthoritativeStateMutation)
{
	auto pending = std::make_shared<BattleUnitsChanged>(pair());
	auto started = std::make_shared<std::promise<void>>();
	auto startedResult = started->get_future();
	auto done = std::make_shared<std::promise<void>>();
	auto completed = done->get_future();
	{
		NativeBonusGraphAccess graphAccess;
		boost::asio::post(authority->getNetworkHandler().getContext(), [this, pending, started, done]
		{
			started->set_value();
			try { authority->applyPack(*pending); done->set_value(); }
			catch(...) { done->set_exception(std::current_exception()); }
		});
		ASSERT_EQ(startedResult.wait_for(std::chrono::seconds(5)), std::future_status::ready);
		EXPECT_EQ(completed.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		// Do NOT poll/notify the client or join while holding graph access. The
		// worker may own state mutex while waiting; that reverse order is invalid.
	}
	ASSERT_EQ(completed.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	ASSERT_NO_THROW(completed.get());
	drain();
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	EXPECT_EQ(authorityWorld.snapshot(), receiverWorld.snapshot());
	EXPECT_TRUE(authorityWorld.first->defended());
	EXPECT_TRUE(authorityWorld.second->defended());
}

TEST_F(PacketUpdatePublicationTest, PreparedOwnerRejectsMissingLaterReentryBeforeAnyFrameOrWorldPrefix)
{
	auto valid = pair();
	auto rejected = valid;
	rejected.changedStacks[1].id = std::numeric_limits<uint32_t>::max();
	onAuthority([this, valid, rejected]() mutable
	{
		battle::PreparedBattleWorldCommit prepared(authorityWorld.state(),
			std::unique_lock<std::shared_mutex>(CGameState::mutex), valid);
		EXPECT_THROW(authority->gh->sendAndApply(rejected), std::logic_error);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	});
	drain();
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
	// Once preparation retires, ordinary legal publication remains available.
	ASSERT_EQ(publish(valid), "");
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
}

TEST_F(PacketUpdatePublicationTest, PreparedOwnerRejectsMalformedLaterReentryBeforeAnyFrameOrWorldPrefix)
{
	auto valid = pair();
	auto rejected = valid;
	rejected.changedStacks[1].data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	onAuthority([this, valid, rejected]() mutable
	{
		battle::PreparedBattleWorldCommit prepared(authorityWorld.state(),
			std::unique_lock<std::shared_mutex>(CGameState::mutex), valid);
		EXPECT_THROW(authority->gh->sendAndApply(rejected), std::logic_error);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	});
	drain();
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
}

namespace
{
std::vector<std::byte> publicationReplayBytes(CGameState & state)
{
	CMemorySerializer memory;
	memory.oser & state.replayLog;
	return memory.extractBuffer();
}

void prepareOwnedOccurrenceRows(PreparedBattleWorldPublication & publication, CGameState & state)
{
	// Explicit actual native owners, not casts from captured const pointers.
	TNodesVector writable;
	for(const auto & battle : state.currentBattles)
	{
		writable.push_back(battle.get());
		for(const auto & stack : battle->stacks)
			writable.push_back(stack.get());
	}
	TNodesVector exactWritable;
	TCNodesVector readonly;
	for(const auto * node : publication.projected().capturedGraphNodes())
		if(const auto found = std::find(writable.begin(), writable.end(), node); found != writable.end())
			exactWritable.push_back(*found);
		else
			readonly.push_back(node);
	publication.prepare(exactWritable, readonly);
}
}

TEST_F(PacketUpdatePublicationTest, ActualServerFactoryRejectsMissingLaterBeforeFramesOrWorldPrefix)
{
	auto invalid = pair();
	invalid.changedStacks[1].id = std::numeric_limits<uint32_t>::max();
	onAuthority([this, invalid]() mutable
	{
		const auto replay = publicationReplayBytes(*authorityWorld.state());
		EXPECT_THROW(authority->prepareBattleWorldPublication(invalid), std::runtime_error);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		EXPECT_EQ(publicationReplayBytes(*authorityWorld.state()), replay);
	});
	drain();
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
}

TEST_F(PacketUpdatePublicationTest, ActualServerFactoryRejectsMalformedLaterBeforeFramesOrWorldPrefix)
{
	auto invalid = pair();
	invalid.changedStacks[1].data["state"]["guardianSpiritHitPoints"].Integer() = -1;
	onAuthority([this, invalid]() mutable
	{
		const auto replay = publicationReplayBytes(*authorityWorld.state());
		EXPECT_THROW(authority->prepareBattleWorldPublication(invalid), std::runtime_error);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		EXPECT_EQ(publicationReplayBytes(*authorityWorld.state()), replay);
	});
	drain();
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
}

TEST_F(PacketUpdatePublicationTest, ActualServerFactoryCommitsWorldThenOneFrameAndCallerWriterProjection)
{
	auto requested = pair();
	onAuthority([this, requested]() mutable
	{
		const auto firstExpected = requested.changedStacks[0].data;
		const auto secondExpected = requested.changedStacks[1].data;
		auto publication = authority->prepareBattleWorldPublication(requested);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		prepareOwnedOccurrenceRows(*publication, *authorityWorld.state());
		ASSERT_TRUE(publication->ready());
		ASSERT_TRUE(publication->commit());
		EXPECT_FALSE(publication->ready());
		EXPECT_FALSE(publication->commit());
		EXPECT_EQ(authorityWorld.first->save(), firstExpected);
		EXPECT_EQ(authorityWorld.second->save(), secondExpected);
		for(size_t index = 0; index < requested.changedStacks.size(); ++index)
		{
			auto projected = index == 0 ? firstExpected : secondExpected;
			projected["state"]["activationMovementBonus"] = JsonNode();
			projected["state"]["veteranPhysicalDamageSinceActivation"] = JsonNode();
			EXPECT_EQ(requested.changedStacks[index].data, projected);
		}
	});
	drain();
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	EXPECT_EQ(authorityWorld.snapshot(), receiverWorld.snapshot());
}

TEST_F(PacketUpdatePublicationTest, ActualServerFactoryRejectsNestedPreparationBeforeStateRelock)
{
	auto requested = pair();
	onAuthority([this, requested]() mutable
	{
		auto publication = authority->prepareBattleWorldPublication(requested);
		EXPECT_THROW(authority->prepareBattleWorldPublication(requested), std::logic_error);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	});
	drain();
	EXPECT_TRUE(receipts.empty());
}

TEST_F(PacketUpdatePublicationTest, IncorrectReadonlyPartitionCancelsFramesAndPreservesWorldReplayAndCaller)
{
	auto requested = pair();
	// A defending-only UPDATE changes scalar state, not graph storage. Make this
	// a genuine graph-write request: placement UPDATE journals the battle root.
	const auto moved = BattleHex(4, 5);
	ASSERT_NE(authorityWorld.first->getPosition(), moved);
	requested.changedStacks[0].data["state"]["position"].Integer() = moved.toInt();
	ASSERT_EQ(requested.changedStacks[0].healthDelta, 0);
	onAuthority([this, requested]() mutable
	{
		const auto caller = requested.changedStacks[0].data;
		const auto replay = publicationReplayBytes(*authorityWorld.state());
		auto publication = authority->prepareBattleWorldPublication(requested);
		const auto generation = PreparedBonusGraphStorage::currentGeneration();
		EXPECT_EQ(publication->projected().findUnit(authorityWorld.first->unitId())->getPosition(), BattleHex(4, 5));
		std::string reason;
		EXPECT_THROW(([&]
		{
			try { publication->prepare({}, publication->projected().capturedGraphNodes()); }
			catch(const std::invalid_argument & error) { reason = error.what(); throw; }
		}()), std::invalid_argument);
		EXPECT_EQ(reason, "Read-only final graph row is an invalidation target");
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
		EXPECT_FALSE(publication->ready());
		EXPECT_FALSE(publication->commit());
		EXPECT_EQ(requested.changedStacks[0].data, caller);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		EXPECT_EQ(publicationReplayBytes(*authorityWorld.state()), replay);
	});
	drain();
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
}

namespace
{
std::vector<std::byte> rawCallerFields(const BattleUnitsChanged & packet)
{
	// Independent test oracle: field DTO only, NOT packet/envelope serialization.
	// Invalid caller mutations must not be validated/normalized or invoke writers.
	CMemorySerializer memory;
	memory.oser & packet.battleID;
	uint64_t count = packet.changedStacks.size();
	memory.oser & count;
	for(const auto & change : packet.changedStacks)
	{
		memory.oser & change.id;
		memory.oser & change.operation;
		memory.oser & change.healthDelta;
		memory.oser & change.data;
	}
	bool chain = packet.rebirthChainConsumption.has_value(), phoenix = packet.phoenixSparkConsumption.has_value();
	memory.oser & chain;
	if(chain)
	{
		memory.oser & packet.rebirthChainConsumption->side;
		memory.oser & packet.rebirthChainConsumption->sourceUnitId;
		memory.oser & packet.rebirthChainConsumption->spawnUnitId;
		memory.oser & packet.rebirthChainConsumption->rollback;
	}
	memory.oser & phoenix;
	if(phoenix)
	{
		memory.oser & packet.phoenixSparkConsumption->side;
		memory.oser & packet.phoenixSparkConsumption->sourceUnitId;
	}
	return memory.extractBuffer();
}
}

TEST_F(PacketUpdatePublicationTest, EveryCallerCarrierMutationRefusesBeforeFramesWorldReplayOrCallerOverwrite)
{
	using Mutate = std::function<void(BattleUnitsChanged &)>;
	const std::vector<std::pair<const char *, Mutate>> mutations{
		{"battleID", [](auto & p) { p.battleID = BattleID(999); }},
		{"unitID", [](auto & p) { ++p.changedStacks[0].id; }},
		{"operation", [](auto & p) { p.changedStacks[0].operation = BattleChanges::EOperation::REMOVE; }},
		{"healthDelta", [](auto & p) { ++p.changedStacks[0].healthDelta; }},
		{"stateValue", [](auto & p) { p.changedStacks[0].data["state"]["defending"].Bool() = false; }},
		{"unknownField", [](auto & p) { p.changedStacks[0].data["callerOnly"] = JsonNode(true); }},
		{"type", [](auto & p) { p.changedStacks[0].data["state"]["defending"] = JsonNode(int64_t{1}); }},
		{"metadata", [](auto & p) { p.changedStacks[0].data["state"].setModScope("changed", false); }},
		{"override", [](auto & p) { p.changedStacks[0].data.setOverrideFlag(true); }},
		{"entryOrder", [](auto & p) { std::swap(p.changedStacks[0], p.changedStacks[1]); }},
		{"entryCount", [](auto & p) { p.changedStacks.pop_back(); }},
		{"chainPresence", [](auto & p) { p.rebirthChainConsumption.emplace(); }},
		{"phoenixPresence", [](auto & p) { p.phoenixSparkConsumption.emplace(); }}
	};
	for(const auto & [name, mutate] : mutations)
	{
		SCOPED_TRACE(name);
		auto requested = pair();
		onAuthority([this, requested, mutate]() mutable
		{
			const auto replay = publicationReplayBytes(*authorityWorld.state());
			auto publication = authority->prepareBattleWorldPublication(requested);
			prepareOwnedOccurrenceRows(*publication, *authorityWorld.state());
			ASSERT_TRUE(publication->ready());
			mutate(requested);
			const auto untouchedCaller = rawCallerFields(requested);
			const auto generation = PreparedBonusGraphStorage::currentGeneration();
			EXPECT_FALSE(publication->ready());
			EXPECT_FALSE(publication->commit());
			EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
			EXPECT_EQ(rawCallerFields(requested), untouchedCaller);
			EXPECT_EQ(publicationReplayBytes(*authorityWorld.state()), replay);
			EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		});
		drain();
		EXPECT_TRUE(receipts.empty());
		EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
	}
}

TEST_F(PacketUpdatePublicationTest, RestoringExactCallerInputBeforeCommitRemainsLegal)
{
	auto requested = pair();
	onAuthority([this, requested]() mutable
	{
		auto publication = authority->prepareBattleWorldPublication(requested);
		prepareOwnedOccurrenceRows(*publication, *authorityWorld.state());
		const auto original = requested.battleID;
		requested.battleID = BattleID(999);
		EXPECT_FALSE(publication->ready());
		requested.battleID = original;
		ASSERT_TRUE(publication->ready());
		ASSERT_TRUE(publication->commit());
	});
	drain();
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
}

TEST_F(PacketUpdatePublicationTest, ExplicitlyStoppedReceiverRefusesValidPreparationWithoutWorldOrWirePrefix)
{
	auto requested = pair();
	ASSERT_FALSE(clientNetwork->getContext().stopped());
	clientNetwork->getContext().stop(); // Explicit stop still wins over outstanding work.
	ASSERT_TRUE(clientNetwork->getContext().stopped());
	onAuthority([this, requested]() mutable
	{
		const auto replay = publicationReplayBytes(*authorityWorld.state());
		try
		{
			authority->prepareBattleWorldPublication(requested);
			ADD_FAILURE() << "Stopped receiver preparation unexpectedly succeeded";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), "Prepared transport binding is closed, stopped or changed");
		}
		catch(...)
		{
			ADD_FAILURE() << "Stopped receiver preparation threw a different exception type";
		}
		EXPECT_EQ(publicationReplayBytes(*authorityWorld.state()), replay);
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
	});
	EXPECT_TRUE(receipts.empty());
	EXPECT_EQ(receiverWorld.snapshot(), receiverBefore);
}

TEST_F(PacketUpdatePublicationTest, ScalarOnlyUpdateMayObserveEveryUnchangedGraphNodeReadOnly)
{
	auto requested = pair();
	onAuthority([this, requested]() mutable
	{
		const auto firstExpected = requested.changedStacks[0].data;
		const auto secondExpected = requested.changedStacks[1].data;
		const auto firstGraph = authorityWorld.first->getAllBonuses(Selector::all, "scalar-only-readonly.first");
		const auto secondGraph = authorityWorld.second->getAllBonuses(Selector::all, "scalar-only-readonly.second");
		const auto generation = PreparedBonusGraphStorage::currentGeneration();
		auto publication = authority->prepareBattleWorldPublication(requested);
		// Real world/mutex and mutable scalar transfer participants are owned by
		// publication; this partition classifies ONLY unchanged graph storage.
		publication->prepare({}, publication->projected().capturedGraphNodes());
		ASSERT_TRUE(publication->ready());
		EXPECT_EQ(authorityWorld.snapshot(), authorityBefore);
		ASSERT_TRUE(publication->commit());
		EXPECT_FALSE(publication->commit());
		EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
		EXPECT_EQ(authorityWorld.first->getAllBonuses(Selector::all, "scalar-only-readonly.first"), firstGraph);
		EXPECT_EQ(authorityWorld.second->getAllBonuses(Selector::all, "scalar-only-readonly.second"), secondGraph);
		EXPECT_EQ(authorityWorld.first->save(), firstExpected);
		EXPECT_EQ(authorityWorld.second->save(), secondExpected);
	});
	drain();
	ASSERT_EQ(receipts.size(), 1u);
	EXPECT_TRUE(receipts.front().error.empty());
	EXPECT_EQ(authorityWorld.snapshot(), receiverWorld.snapshot());
}
