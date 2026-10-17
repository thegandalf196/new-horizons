/*
 * PreparedConsumedBattleReplyTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../client/replay/ClientReceiveAccess.h"
#include "../../client/replay/ClientPacketReceiveSession.h"
#include "../../client/replay/PreparedClientBattleReplay.h"
#include "../../client/netlag/PreparedNetworkLagRetirement.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/BinarySerializer.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include <atomic>
#include <thread>
#include <type_traits>

namespace
{
static_assert(!std::is_move_constructible_v<ClientReceiveAccess>);
static_assert(!std::is_copy_constructible_v<ClientReceiveAccess>);
static_assert(std::is_abstract_v<ClientReplaySessionReadiness>);
static_assert(std::is_base_of_v<ClientReplaySessionReadiness, ClientPacketReceiveSession>);
static_assert(std::is_final_v<ClientPacketReceiveSession>);
static_assert(!std::is_convertible_v<ClientPacketReceiveSession *, ClientReplaySessionReadiness *>);
static_assert(noexcept(std::declval<const ClientReplaySessionReadiness &>().ready(
	std::declval<const BattleUnitsChanged &>())));
static_assert(noexcept(std::declval<PreparedClientBattleReplay &>().commitConsumedReply()));

std::shared_ptr<CGameState> world(bool configured = false)
{
	auto state = std::make_shared<CGameState>();
	state->day = 3;
	if(configured) state->replayLog.configure(false, 2);
	state->replayLog.beginDay(3, {std::byte{7}});
	return state;
}

std::vector<std::byte> bytes(const ReplayLog & log)
{
	CMemorySerializer memory;
	memory.oser & log;
	return memory.extractBuffer();
}

BattleUnitsChanged packet(int id = 0)
{
	BattleUnitsChanged result;
	result.battleID = BattleID(id);
	return result;
}

NetworkLagPredictedPack predicted(BattleUnitsChanged packet)
{
	NetworkLagPredictedPack result;
	BinarySerializer serializer(&result);
	const CPackForClient & base = packet; // exact verifier pointer type; private writer input
	serializer & &base;
	return result;
}

void begin(NetworkLagReplyState & state, uint32_t id = 5)
{
	ArrangeStacks request;
	request.requestID = id;
	request.player = PlayerColor(0);
	state.beginPrediction(request);
}

void open(NetworkLagReplyState & state, uint32_t id = 5)
{
	PackageReceived received;
	received.requestID = id;
	received.player = PlayerColor(0);
	state.openReplyPackage(received);
}

void seed(NetworkLagReplyState & state, const BattleUnitsChanged & caller)
{
	begin(state);
	state.appendPrediction(predicted(caller));
	open(state);
}

struct RetiredProbe final : CPackForClient
{
	int & count;
	bool & accepting;
	bool & destroyedInTail;
	RetiredProbe(int & count, bool & accepting, bool & bad)
		: count(count), accepting(accepting), destroyedInTail(bad) {}
	~RetiredProbe() override { ++count; destroyedInTail |= accepting; }
};
}

TEST(PreparedConsumedBattleReplyTest, SameBatchMatchesActualUnconditionalOuterWithBothConfiguredModes)
{
	for(bool configured : {false, true})
	{
		auto state = world(configured);
		std::mutex mutex;
		NetworkLagReplyState source(&mutex);
		auto caller = packet();
		seed(source, caller);
		auto expected = state->replayLog;
		auto scalar = caller;
		expected.recordPack(scalar, *state); // verifyReply CONSUMED: NO configured GS.apply
		const auto before = bytes(state->replayLog);
		std::unique_lock held(mutex);
		ClientReceiveAccess receive(std::move(held), &mutex);
		EXPECT_FALSE(held.owns_lock());
		auto & entry = receive.prepareConsumed(state, caller, source);
		EXPECT_TRUE(receive.owns(entry));
		EXPECT_EQ(bytes(state->replayLog), before);
		EXPECT_TRUE(entry.ready());
		EXPECT_FALSE(entry.commitReplayOnly()); // cannot accept replay ahead of queue
		ASSERT_TRUE(entry.commitConsumedReply());
		EXPECT_EQ(bytes(state->replayLog), bytes(expected));
		EXPECT_EQ(source.inspectBattleReply(caller).result, BattleReplyInspection::Result::APPLY);
		EXPECT_FALSE(entry.commitConsumedReply());
		EXPECT_TRUE(receive.heldOnThisThread());
	}
}

TEST(PreparedConsumedBattleReplyTest, CallerProjectionIsPublishedOnceWithoutBattleApplyOrGraphCAS)
{
	auto state = world();
	ASSERT_TRUE(state->currentBattles.empty());
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	caller.changedStacks.emplace_back(991, BattleChanges::EOperation::UPDATE);
	caller.changedStacks.back().data["state"]["defending"].Bool() = true;
	seed(source, caller);
	const auto original = caller.changedStacks.back().data;
	auto scalar = caller;
	auto expected = state->replayLog;
	expected.recordPack(scalar, *state);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	auto & entry = receive.prepareConsumed(state, caller, source);
	EXPECT_EQ(caller.changedStacks.back().data, original);
	EXPECT_THROW(entry.adoptIncomingBattleWorld(), std::logic_error);
	ASSERT_TRUE(entry.commitConsumedReply());
	EXPECT_EQ(caller.changedStacks.back().data, scalar.changedStacks.back().data);
	EXPECT_EQ(bytes(state->replayLog), bytes(expected));
	EXPECT_TRUE(state->currentBattles.empty()); // missing battle deliberately never looked up
}

TEST(PreparedConsumedBattleReplyTest, MutatedCallerRefusesBothTailsAndDoesNotErasePrediction)
{
	auto state = world();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	seed(source, caller);
	const auto before = bytes(state->replayLog);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	auto & entry = receive.prepareConsumed(state, caller, source);
	caller.battleID = BattleID(91);
	EXPECT_FALSE(entry.ready());
	EXPECT_FALSE(entry.commitConsumedReply());
	EXPECT_EQ(caller.battleID, BattleID(91));
	EXPECT_EQ(bytes(state->replayLog), before);
	EXPECT_TRUE(source.matchesNextReply(packet()));
}

TEST(PreparedConsumedBattleReplyTest, ReplayInvalidationRefusesBeforeQueueUnlink)
{
	auto state = world();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	seed(source, caller);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	auto & entry = receive.prepareConsumed(state, caller, source);
	state->replayLog.beginDay(4, {std::byte{8}});
	const auto external = bytes(state->replayLog);
	EXPECT_FALSE(entry.commitConsumedReply());
	EXPECT_EQ(bytes(state->replayLog), external);
	EXPECT_TRUE(source.matchesNextReply(caller));
}

TEST(PreparedConsumedBattleReplyTest, QueueStalenessRefusesReplayAndCallerProjection)
{
	auto state = world();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	seed(source, caller);
	const auto before = bytes(state->replayLog);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	auto & entry = receive.prepareConsumed(state, caller, source);
	source.appendPrediction(predicted(packet(1)));
	EXPECT_FALSE(entry.ready());
	EXPECT_FALSE(entry.commitConsumedReply());
	EXPECT_EQ(bytes(state->replayLog), before);
	EXPECT_TRUE(source.matchesNextReply(caller));
}

TEST(PreparedConsumedBattleReplyTest, SourceRetirementRefusesWithoutDanglingQueueAccess)
{
	auto state = world();
	std::mutex mutex;
	auto source = std::make_unique<NetworkLagReplyState>(&mutex);
	auto caller = packet();
	seed(*source, caller);
	const auto before = bytes(state->replayLog);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	auto & entry = receive.prepareConsumed(state, caller, *source);
	source.reset();
	EXPECT_FALSE(entry.ready());
	EXPECT_FALSE(entry.commitConsumedReply());
	EXPECT_EQ(bytes(state->replayLog), before);
}

TEST(PreparedConsumedBattleReplyTest, WrongOrAbsentActualLockCannotStartCapture)
{
	auto state = world();
	std::mutex first, second;
	NetworkLagReplyState source(&first);
	auto caller = packet();
	seed(source, caller);
	const auto before = bytes(state->replayLog);
	{
		std::unique_lock held(first);
		ClientReceiveAccess wrong(std::move(held), &second);
		EXPECT_FALSE(wrong.heldOnThisThread());
		EXPECT_THROW(wrong.prepareConsumed(state, caller, source), std::logic_error);
	}
	std::unique_lock<std::mutex> absent;
	ClientReceiveAccess noEngine(std::move(absent), nullptr);
	EXPECT_THROW(noEngine.prepareConsumed(state, caller, source), std::logic_error);
	EXPECT_EQ(bytes(state->replayLog), before);
	EXPECT_TRUE(source.matchesNextReply(caller));
}

TEST(PreparedConsumedBattleReplyTest, ForeignQueueBindingCannotUseTheRetainedReceiveLock)
{
	auto state = world();
	std::mutex actual, foreign;
	NetworkLagReplyState source(&foreign);
	auto caller = packet();
	seed(source, caller);
	const auto before = bytes(state->replayLog);
	std::unique_lock held(actual);
	ClientReceiveAccess receive(std::move(held), &actual);
	EXPECT_THROW(receive.prepareConsumed(state, caller, source), std::invalid_argument);
	EXPECT_EQ(bytes(state->replayLog), before);
	EXPECT_TRUE(receive.heldOnThisThread());
	EXPECT_TRUE(source.matchesNextReply(caller));
}

TEST(PreparedConsumedBattleReplyTest, MatchingPartialFrontKeepsLaterPacketAndRequestOrder)
{
	auto state = world();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	begin(source);
	source.appendPrediction(predicted(caller));
	source.appendPrediction(predicted(packet(1)));
	begin(source, 6);
	source.appendPrediction(predicted(packet(2)));
	open(source);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	auto & entry = receive.prepareConsumed(state, caller, source);
	ASSERT_TRUE(entry.commitConsumedReply());
	EXPECT_TRUE(source.matchesNextReply(packet(1)));
	EXPECT_THROW(receive.prepareConsumed(state, caller, source), std::logic_error);
}

TEST(PreparedConsumedBattleReplyTest, WriterFailureLeavesNoReplayOrQueueAcceptanceAndKeepsLock)
{
	auto state = world();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	seed(source, caller);
	caller.changedStacks.emplace_back(1, BattleChanges::EOperation::UPDATE);
	caller.changedStacks.back().data["state"]["luckyOwnAttackSequence"].String() = "invalid";
	const auto original = caller.changedStacks.back().data;
	const auto before = bytes(state->replayLog);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	EXPECT_THROW(receive.prepareConsumed(state, caller, source), std::runtime_error);
	EXPECT_EQ(bytes(state->replayLog), before);
	EXPECT_EQ(caller.changedStacks.back().data, original);
	EXPECT_TRUE(source.matchesNextReply(packet()));
	EXPECT_TRUE(receive.heldOnThisThread());
}

TEST(PreparedConsumedBattleReplyTest, RetainedOriginalsRetireAfterNativeScopeAndBeforeInterfaceRelease)
{
	auto state = world();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	int destroyed = 0;
	bool accepting = false, destroyedInTail = false;
	begin(source);
	auto written = predicted(caller);
	written.rollbackPacks.push_back(std::make_unique<RetiredProbe>(destroyed, accepting, destroyedInTail));
	source.appendPrediction(std::move(written));
	open(source);
	{
		std::unique_lock held(mutex);
		ClientReceiveAccess receive(std::move(held), &mutex);
		auto & entry = receive.prepareConsumed(state, caller, source);
		accepting = true;
		ASSERT_TRUE(entry.commitConsumedReply());
		accepting = false;
		EXPECT_EQ(destroyed, 0);
		EXPECT_FALSE(destroyedInTail);
		EXPECT_TRUE(receive.heldOnThisThread());
	}
	EXPECT_EQ(destroyed, 1);
	EXPECT_FALSE(destroyedInTail);
}

TEST(PreparedConsumedBattleReplyTest, SameThreadOwnerContinuouslyExcludesAnotherInterfaceUserUntilRetirement)
{
	std::mutex mutex;
	std::unique_lock held(mutex);
	{
		ClientReceiveAccess receive(std::move(held), &mutex);
		std::atomic<bool> entered{false};
		std::thread probe([&] { if(mutex.try_lock()) { entered = true; mutex.unlock(); } });
		probe.join(); // CPU test only, no native/root/state participant or GUI held
		EXPECT_FALSE(entered.load());
		EXPECT_TRUE(receive.heldOnThisThread());
		EXPECT_FALSE(held.owns_lock());
	}
	std::atomic<bool> entered{false};
	std::thread probe([&] { if(mutex.try_lock()) { entered = true; mutex.unlock(); } });
	probe.join();
	EXPECT_TRUE(entered.load());
}

class PreparedConsumedReplySnapshotTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0));
		startWithMap(std::move(builder));
	}
};

TEST_F(PreparedConsumedReplySnapshotTest, EmptyChapterAcceptsOnlyCapturedPrewriterSnapshotAndOneOuterRecord)
{
	startGame();
	auto state = gameState();
	state->replayLog = ReplayLog{};
	const auto expectedSnapshot = state->saveToMemory();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	seed(source, caller);
	auto scalar = caller;
	const auto expectedPack = ReplayPackSerializer::write(scalar);
	std::unique_lock held(mutex);
	ClientReceiveAccess receive(std::move(held), &mutex);
	auto & entry = receive.prepareConsumed(state, caller, source);
	EXPECT_TRUE(state->replayLog.empty());
	ASSERT_TRUE(entry.commitConsumedReply());
	ASSERT_EQ(state->replayLog.getChapters().size(), 1);
	const auto & chapter = state->replayLog.getChapters().front();
	EXPECT_EQ(chapter.gamestateSnapshot, expectedSnapshot);
	ASSERT_EQ(chapter.packs.size(), 1);
	EXPECT_EQ(chapter.packs.front(), expectedPack);
	EXPECT_FALSE(state->replayLog.isRecordingPacks());
	EXPECT_EQ(source.inspectBattleReply(caller).result, BattleReplyInspection::Result::APPLY);
}

TEST_F(PreparedConsumedReplySnapshotTest, WriterFailureAfterEmptySnapshotLeavesOriginalQueueAndNoChapter)
{
	startGame();
	auto state = gameState();
	state->replayLog = ReplayLog{};
	const auto before = state->saveToMemory();
	std::mutex mutex;
	NetworkLagReplyState source(&mutex);
	auto caller = packet();
	seed(source, caller);
	caller.changedStacks.emplace_back(1, BattleChanges::EOperation::UPDATE);
	caller.changedStacks.back().data["state"]["luckyOwnAttackSequence"].String() = "invalid";
	const auto original = caller.changedStacks.back().data;
	{
		std::unique_lock held(mutex);
		ClientReceiveAccess receive(std::move(held), &mutex);
		EXPECT_THROW(receive.prepareConsumed(state, caller, source), std::runtime_error);
		EXPECT_TRUE(state->replayLog.empty());
		EXPECT_EQ(caller.changedStacks.back().data, original);
		EXPECT_TRUE(source.matchesNextReply(packet()));
	}
	EXPECT_EQ(state->saveToMemory(), before);
}
