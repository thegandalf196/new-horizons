/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/gameState/PreparedReplayAppend.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
std::vector<std::byte> replayBytes(const ReplayLog & log)
{
	CMemorySerializer wire;
	wire.oser & log;
	return wire.extractBuffer();
}
}

TEST(RollingReplayPolicy, DefaultAndFullGameOverrideKeepEnabledBehavior)
{
	ReplayLog defaultPolicy;
	EXPECT_TRUE(defaultPolicy.isCaptureEnabled());
	defaultPolicy.configure(false, 1);
	EXPECT_TRUE(defaultPolicy.isRecordingPacks());
	ReplayLog selectedFull;
	selectedFull.configure(true, 1, false);
	EXPECT_TRUE(selectedFull.isCaptureEnabled());
	EXPECT_TRUE(selectedFull.isRecordingPacks());
	EXPECT_TRUE(selectedFull.isRecordingEntireGame());
	selectedFull.beginDay(1, {std::byte{1}});
	EXPECT_FALSE(selectedFull.empty());
}

TEST(RollingReplayPolicy, DisabledDirectAndPreparedEntriesNeverRequestSnapshotOrEncodePackets)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	ReplayLog disabled;
	disabled.configure(false, 1, false);
	EXPECT_FALSE(disabled.isCaptureEnabled());
	EXPECT_FALSE(disabled.isRecordingPacks());
	CGameState unavailable; // Recording would attempt an invalid empty-world snapshot.
	NewTurn turn;
	turn.day = 2;
	ASSERT_NO_THROW(disabled.recordPack(turn, unavailable));
	EXPECT_TRUE(disabled.empty());
	int snapshots = 0;
	const auto snapshot = [&]() -> ReplayPayload { ++snapshots; throw std::runtime_error("Unexpected snapshot"); };
	BattleUnitsChanged packet;
	PreparedReplayAppend ordinary(disabled, packet, 2, snapshot);
	EXPECT_TRUE(ordinary.commit());
	PreparedReplayAppend::Records records{ReplayPayload{{std::byte{1}}, ESerializationVersion::CURRENT}};
	PreparedReplayAppend outer(disabled, records, 2, snapshot, PreparedReplayAppend::RecordingMode::UNCONDITIONAL);
	EXPECT_TRUE(outer.commit());
	EXPECT_EQ(snapshots, 0);
	EXPECT_TRUE(disabled.empty());
	EXPECT_THROW(disabled.beginDay(2, {std::byte{1}}), std::runtime_error);
}

TEST(RollingReplayPolicy, SavedPolicySurvivesLoadCopyMoveAndDoesNotResumeFromLocalRetentionChanges)
{
	ReplayLog disabled;
	disabled.configure(false, 4, false);
	CMemorySerializer wire;
	wire.oser & disabled;
	ReplayLog loaded;
	wire.iser & loaded;
	EXPECT_FALSE(loaded.isCaptureEnabled());
	loaded.reconfigureOnLoad(9);
	EXPECT_FALSE(loaded.isRecordingPacks());
	auto copied = loaded;
	ReplayLog moved = std::move(copied);
	EXPECT_FALSE(moved.isCaptureEnabled());
	EXPECT_EQ(replayBytes(moved), replayBytes(loaded));
	ReplayLog assigned;
	assigned = loaded;
	EXPECT_FALSE(assigned.isCaptureEnabled());
	ReplayLog moveAssigned;
	moveAssigned = std::move(assigned);
	EXPECT_FALSE(moveAssigned.isCaptureEnabled());
}

TEST(RollingReplayPolicy, OptOutCannotDiscardExistingHistoryAndUnsupportedWritersRejectBeforePrefix)
{
	ReplayLog enabled;
	enabled.configure(false, 1);
	enabled.beginDay(1, {std::byte{1}});
	const auto before = replayBytes(enabled);
	EXPECT_THROW(enabled.configure(false, 1, false), std::runtime_error);
	EXPECT_EQ(replayBytes(enabled), before);
	EXPECT_TRUE(enabled.isRecordingPacks());
	ReplayLog disabled;
	disabled.configure(false, 1, false);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT;
	EXPECT_THROW(old.oser & disabled, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(RollingReplayPolicy, MalformedDisabledHistoryLoadPreservesTheOriginalLog)
{
	ReplayLog original;
	original.configure(true, 2);
	original.beginDay(1, {std::byte{1}});
	const auto before = replayBytes(original);
	CMemorySerializer malformed;
	std::vector<ReplayChapter> chapters(1);
	chapters.front().day = 1;
	malformed.oser & chapters;
	malformed.oser & false; // whole-game recording
	malformed.oser & uint32_t{1};
	malformed.oser & false; // opt-out contradicts the nonempty history
	EXPECT_THROW(malformed.iser & original, std::runtime_error);
	EXPECT_EQ(replayBytes(original), before);
}
