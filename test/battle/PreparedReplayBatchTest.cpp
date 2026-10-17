/*
 * PreparedReplayBatchTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/gameState/PreparedReplayAppend.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <new>

namespace
{
std::vector<std::byte> bytes(const ReplayLog & log)
{
	CMemorySerializer serializer;
	serializer.oser & log;
	return serializer.extractBuffer();
}

ReplayLog existing(bool configured)
{
	ReplayLog log;
	if(configured) log.configure(false, 2);
	log.beginDay(3, {std::byte{7}});
	log.addPack({std::byte{1}});
	return log;
}
}

TEST(PreparedReplayBatchTest, UnconditionalFalseAndTrueModesAppendWholeOrderedBatchWithOneReservation)
{
	for(bool configured : {false, true})
	{
		auto log = existing(configured);
		auto expected = log;
		PreparedReplayAppend::Records records{{std::byte{2}}, {std::byte{3}}, {std::byte{4}}};
		for(const auto & record : records) expected.addPack(record);
		const auto before = bytes(log);
		int snapshots = 0;
		PreparedReplayAppend plan(log, records, 3, [&] { ++snapshots; return std::vector<std::byte>{}; },
			PreparedReplayAppend::RecordingMode::UNCONDITIONAL);
		EXPECT_EQ(bytes(log), before);
		ASSERT_TRUE(plan.commit());
		EXPECT_EQ(bytes(log), bytes(expected));
		EXPECT_EQ(log.isRecordingPacks(), configured);
		EXPECT_EQ(snapshots, 0);
		EXPECT_FALSE(plan.commit());
	}
}

TEST(PreparedReplayBatchTest, ConfiguredModeDoesNotInventRecordingWhenDisabled)
{
	ReplayLog log;
	const auto before = bytes(log);
	PreparedReplayAppend plan(log, {{std::byte{3}}}, 3,
		[]() -> std::vector<std::byte> { throw std::bad_alloc(); },
		PreparedReplayAppend::RecordingMode::WHEN_CONFIGURED);
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(bytes(log), before);
	EXPECT_FALSE(log.isRecordingPacks());
}

TEST(PreparedReplayBatchTest, EmptyUnconfiguredChapterPublishesExactPrepacketSnapshotOnce)
{
	ReplayLog log;
	auto expected = log;
	const std::vector<std::byte> snapshot{std::byte{9}};
	PreparedReplayAppend::Records records{{std::byte{2}}, {std::byte{3}}};
	expected.beginDay(3, snapshot);
	for(const auto & record : records) expected.addPack(record);
	int snapshots = 0;
	PreparedReplayAppend plan(log, records, 3, [&] { ++snapshots; return snapshot; },
		PreparedReplayAppend::RecordingMode::UNCONDITIONAL);
	EXPECT_TRUE(log.empty());
	ASSERT_TRUE(plan.commit());
	EXPECT_EQ(bytes(log), bytes(expected));
	EXPECT_EQ(snapshots, 1);
	EXPECT_FALSE(log.isRecordingPacks());
}

TEST(PreparedReplayBatchTest, SnapshotFailureAndConfigurationStalenessPublishNoBatchPrefix)
{
	ReplayLog log;
	const auto before = bytes(log);
	EXPECT_THROW((PreparedReplayAppend(log, PreparedReplayAppend::Records{{std::byte{2}}}, 3,
		[]() -> std::vector<std::byte> { throw std::bad_alloc(); },
		PreparedReplayAppend::RecordingMode::UNCONDITIONAL)), std::bad_alloc);
	EXPECT_EQ(bytes(log), before);
	log = existing(false);
	PreparedReplayAppend plan(log, PreparedReplayAppend::Records{{std::byte{2}}}, 3,
		[] { return std::vector<std::byte>{}; }, PreparedReplayAppend::RecordingMode::UNCONDITIONAL);
	log.configure(false, 2);
	const auto changed = bytes(log);
	EXPECT_FALSE(plan.commit());
	EXPECT_EQ(bytes(log), changed);
}

TEST(PreparedReplayBatchTest, SingleAndBatchPlansShareOneInvalidationIdentity)
{
	auto log = existing(true);
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	PreparedReplayAppend single(log, packet, 3, [] { return std::vector<std::byte>{}; });
	PreparedReplayAppend batch(log, PreparedReplayAppend::Records{{std::byte{2}}, {std::byte{3}}}, 3,
		[] { return std::vector<std::byte>{}; }, PreparedReplayAppend::RecordingMode::UNCONDITIONAL);
	ASSERT_TRUE(batch.commit());
	const auto committed = bytes(log);
	EXPECT_FALSE(single.commit());
	EXPECT_EQ(bytes(log), committed);
}
