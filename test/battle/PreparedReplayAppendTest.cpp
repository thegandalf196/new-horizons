/*
 * PreparedReplayAppendTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../lib/gameState/PreparedReplayAppend.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace test
{
namespace
{
std::vector<std::byte> replayBytes(const ReplayLog & log)
{
	CMemorySerializer memory;
	memory.oser & log;
	return memory.extractBuffer();
}

ReplayLog existingReplay()
{
	ReplayLog log;
	log.configure(true, 2);
	log.beginDay(3, {std::byte{7}});
	log.addTurn(PlayerColor(0));
	log.addPack({std::byte{5}});
	return log;
}

BattleUnitsChanged ordinaryPacket()
{
	BattleUnitsChanged pack;
	pack.battleID = BattleID(0);
	return pack;
}
}

TEST(PreparedReplayAppendTest, ExistingChapterMatchesLegacyBytesOrderAndSingleUse)
{
	auto log = existingReplay();
	auto expected = log;
	auto pack = ordinaryPacket();
	const auto encoded = ReplayPackSerializer::write(pack);
	expected.addPack(encoded);
	const auto before = replayBytes(log);
	const auto * retainedPayload = log.getChapters().back().packs.front().data();
	int snapshots = 0;
	PreparedReplayAppend plan(log, pack, 3, [&] { ++snapshots; return std::vector<std::byte>{}; });
	EXPECT_EQ(replayBytes(log), before);
	ASSERT_TRUE(plan.readyToCommit());
	static_assert(noexcept(plan.commit()));
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(replayBytes(log), replayBytes(expected));
	EXPECT_EQ(log.getChapters().back().packs.back(), encoded);
	EXPECT_EQ(log.getChapters().back().packs.front().data(), retainedPayload);
	EXPECT_EQ(snapshots, 0);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
}

TEST(PreparedReplayAppendTest, DisabledSkipsSnapshotAndInvalidPacketWriter)
{
	ReplayLog log;
	auto pack = ordinaryPacket();
	pack.changedStacks.emplace_back(1, BattleChanges::EOperation::UPDATE);
	pack.changedStacks.back().data["state"]["luckyOwnAttackSequence"].String() = "invalid";
	int snapshots = 0;
	const auto before = replayBytes(log);
	PreparedReplayAppend plan(log, pack, 1, [&]() -> std::vector<std::byte>
	{
		++snapshots;
		throw std::bad_alloc();
	});
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(snapshots, 0);
	EXPECT_EQ(replayBytes(log), before);
	EXPECT_FALSE(plan.commit());
}

TEST(PreparedReplayAppendTest, EmptyChapterMatchesLegacySnapshotAndPackBytes)
{
	ReplayLog log;
	log.configure(false, 1);
	auto expected = log;
	auto pack = ordinaryPacket();
	const std::vector<std::byte> snapshot{std::byte{9}, std::byte{4}};
	expected.beginDay(5, snapshot);
	expected.addPack(ReplayPackSerializer::write(pack));
	const auto before = replayBytes(log);
	int snapshots = 0;
	PreparedReplayAppend plan(log, pack, 5, [&] { ++snapshots; return snapshot; });
	EXPECT_TRUE(log.empty());
	EXPECT_EQ(replayBytes(log), before);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(snapshots, 1);
	EXPECT_EQ(replayBytes(log), replayBytes(expected));
}

TEST(PreparedReplayAppendTest, SnapshotAllocationFailurePublishesNoChapterOrPack)
{
	ReplayLog log;
	log.configure(true, 2);
	const auto before = replayBytes(log);
	auto pack = ordinaryPacket();
	auto failAllocation = []() -> std::vector<std::byte>
	{
		throw std::bad_alloc();
	};
	EXPECT_THROW((PreparedReplayAppend(log, pack, 3, failAllocation)), std::bad_alloc);
	EXPECT_EQ(replayBytes(log), before);
	EXPECT_TRUE(log.isRecordingPacks());
	EXPECT_TRUE(log.empty());
}

TEST(PreparedReplayAppendTest, PacketEncodingFailurePublishesNoPreparedSnapshot)
{
	ReplayLog log;
	log.configure(true, 2);
	auto pack = ordinaryPacket();
	pack.changedStacks.emplace_back(1, BattleChanges::EOperation::UPDATE);
	pack.changedStacks.back().data["state"]["luckyOwnAttackSequence"].String() = "invalid";
	const auto before = replayBytes(log);
	int snapshots = 0;
	auto snapshot = [&]
	{
		++snapshots;
		return std::vector<std::byte>{std::byte{8}};
	};
	EXPECT_THROW((PreparedReplayAppend(log, pack, 3, snapshot)), std::runtime_error);
	EXPECT_EQ(snapshots, 1);
	EXPECT_EQ(replayBytes(log), before);
}

TEST(PreparedReplayAppendTest, InterveningAppendRefusesWithoutUndoingThatAppend)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend plan(log, pack, 3, [] { return std::vector<std::byte>{}; });
	log.addPack({std::byte{6}});
	const auto intervening = replayBytes(log);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(replayBytes(log), intervening);
}

TEST(PreparedReplayAppendTest, CompetingPlanConsumesMutationIdentityOnlyOnce)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend first(log, pack, 3, [] { return std::vector<std::byte>{}; });
	PreparedReplayAppend second(log, pack, 3, [] { return std::vector<std::byte>{}; });
	ASSERT_TRUE(first.commit());
	const auto once = replayBytes(log);
	EXPECT_FALSE(second.commit());
	EXPECT_EQ(replayBytes(log), once);
}

TEST(PreparedReplayAppendTest, LoadingEvenIdenticalBytesInvalidatesPreparedAppend)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend plan(log, pack, 3, [] { return std::vector<std::byte>{}; });
	CMemorySerializer memory;
	memory.oser & log;
	memory.iser & log;
	const auto reloaded = replayBytes(log);
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(replayBytes(log), reloaded);
}

TEST(PreparedReplayAppendTest, ConfigurationAndTurnChangesInvalidatePreparedAppend)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend first(log, pack, 3, [] { return std::vector<std::byte>{}; });
	log.endTurn(PlayerColor(0));
	EXPECT_FALSE(first.commit());
	PreparedReplayAppend second(log, pack, 3, [] { return std::vector<std::byte>{}; });
	log.reconfigureOnLoad(4);
	EXPECT_FALSE(second.commit());
}

TEST(PreparedReplayAppendTest, EmptyChapterRetainsLegacyExtremeRetentionSemantics)
{
	ReplayLog log;
	log.configure(false, std::numeric_limits<uint32_t>::max());
	auto expected = log;
	auto pack = ordinaryPacket();
	expected.beginDay(1, {});
	expected.addPack(ReplayPackSerializer::write(pack));
	PreparedReplayAppend plan(log, pack, 1, [] { return std::vector<std::byte>{}; });
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(replayBytes(log), replayBytes(expected));
}

TEST(PreparedReplayAppendTest, MutationDuringSnapshotRefusesInsteadOfOverwritingNewLog)
{
	ReplayLog log;
	log.configure(false, 1);
	auto pack = ordinaryPacket();
	auto mutateLog = [&]
	{
		log.beginDay(8, {});
		return std::vector<std::byte>{};
	};
	EXPECT_THROW((PreparedReplayAppend(log, pack, 1, mutateLog)), std::runtime_error);
	ASSERT_EQ(log.getChapters().size(), 1u);
	EXPECT_EQ(log.getChapters().front().day, 8u);
	EXPECT_TRUE(log.getChapters().front().packs.empty());
}

TEST(PreparedReplayAppendTest, CopyAssignmentCannotResurrectAReservation)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend plan(log, pack, 3, [] { return std::vector<std::byte>{}; });
	const auto clone = log;
	log = clone;
	const auto replaced = replayBytes(log);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(replayBytes(log), replaced);
	EXPECT_EQ(replaced, replayBytes(clone));
}

TEST(PreparedReplayAppendTest, MoveAssignmentFromCloneCannotResurrectAReservation)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend plan(log, pack, 3, [] { return std::vector<std::byte>{}; });
	auto clone = log;
	const auto expected = replayBytes(clone);
	log = std::move(clone);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(replayBytes(log), expected);
}

TEST(PreparedReplayAppendTest, SwapWithCloneCannotResurrectAReservation)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend plan(log, pack, 3, [] { return std::vector<std::byte>{}; });
	auto clone = log;
	const auto expected = replayBytes(log);
	std::swap(log, clone);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(replayBytes(log), expected);
	EXPECT_EQ(replayBytes(clone), expected);
}

TEST(PreparedReplayAppendTest, MovingOriginalOutAndBackInvalidatesItsOldPlan)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend plan(log, pack, 3, [] { return std::vector<std::byte>{}; });
	const auto expected = replayBytes(log);
	ReplayLog temporary(std::move(log));
	log = std::move(temporary);
	EXPECT_FALSE(plan.readyToCommit());
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(replayBytes(log), expected);
}

TEST(PreparedReplayAppendTest, CopyingSourceDoesNotInvalidateItsOwnedReservation)
{
	auto log = existingReplay();
	auto pack = ordinaryPacket();
	PreparedReplayAppend first(log, pack, 3, [] { return std::vector<std::byte>{}; });
	auto clone = log;
	PreparedReplayAppend second(clone, pack, 3, [] { return std::vector<std::byte>{}; });
	ASSERT_TRUE(first.commit());
	ASSERT_TRUE(second.commit());
	EXPECT_EQ(replayBytes(log), replayBytes(clone));
}

TEST(PreparedReplayAppendTest, EmptySnapshotRoundtripPrecedesFreshReservation)
{
	ReplayLog log;
	log.configure(true, 2);
	ReplayLog expected = log;
	auto pack = ordinaryPacket();
	// Same log-value roundtrip as saveToMemory, completed before plan capture.
	ReplayLog snapshotBackup;
	std::swap(snapshotBackup, log);
	const std::vector<std::byte> snapshot{std::byte{4}};
	std::swap(snapshotBackup, log);
	PreparedReplayAppend plan(log, pack, 3, [&] { return snapshot; });
	expected.beginDay(3, snapshot);
	expected.addPack(ReplayPackSerializer::write(pack));
	ASSERT_TRUE(plan.readyToCommit());
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(replayBytes(log), replayBytes(expected));
}

TEST(PreparedReplayAppendTest, TypedGameStateFactoryRetainsExistingChapterBytes)
{
	CGameState gs;
	gs.day = 3;
	gs.replayLog = existingReplay();
	auto expected = gs.replayLog;
	auto pack = ordinaryPacket();
	expected.addPack(ReplayPackSerializer::write(pack));
	auto plan = prepareBattleReplayAppend(gs, pack);
	ASSERT_TRUE(plan->readyToCommit());
	ASSERT_TRUE(plan->commit());
	EXPECT_EQ(replayBytes(gs.replayLog), replayBytes(expected));
	EXPECT_FALSE(plan->commit());
}
}
