/*
 * PreparedReplayAppend.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "ReplayLog.h"
#include <functional>
#include <memory>

struct BattleUnitsChanged;
class CGameState;
class PreparedClientBattleReplay;
namespace battle { class PreparedBattleWorldCommit; }

/// Storage phase only: caller retains exclusive world/log/packet ownership until
/// consumption. Does not apply units or authorize publication. Snapshot provider
/// must restore its world/log on failure. The typed factory below restores the
/// empty replay log if the legacy saveToMemory snapshot throws after its swap.
/// Other log mutations invalidate this plan; no serialized token is introduced.
/// Moving/assigning/swapping the owner invalidates its reservation; copying the
/// owner alone does not invalidate the source, but never copies the token.
class DLL_LINKAGE PreparedReplayAppend final
{
public:
	using Records = std::vector<ReplayPayload>;
	enum class RecordingMode { WHEN_CONFIGURED, UNCONDITIONAL };
private:
	friend class battle::PreparedBattleWorldCommit;
	friend class PreparedClientBattleReplay;
	void exchangePreparedStorage() noexcept;
	ReplayLog & owner;
	std::shared_ptr<const unsigned char> stamp;
	std::shared_ptr<const unsigned char> retiredStamp;
	Records encoded;
	std::vector<ReplayChapter> firstChapters;
	bool configured = false;
	bool recording = false;
	bool opensChapter = false;
	bool consumed = false;
	void reservePacks(size_t count);

public:
	using SnapshotProvider = std::function<ReplayPayload()>;
	PreparedReplayAppend(ReplayLog & log, const BattleUnitsChanged & pack,
		uint32_t day, const SnapshotProvider & snapshot);
	/// Preencoded records under ONE reservation. Capture a real pre-packet
	/// snapshot BEFORE encoding when writers can mutate their private input.
	PreparedReplayAppend(ReplayLog & log, Records records, uint32_t day,
		const SnapshotProvider & snapshot, RecordingMode mode);
	PreparedReplayAppend(const PreparedReplayAppend &) = delete;
	PreparedReplayAppend & operator=(const PreparedReplayAppend &) = delete;
	PreparedReplayAppend(PreparedReplayAppend &&) = delete;
	PreparedReplayAppend & operator=(PreparedReplayAppend &&) = delete;
	bool readyToCommit() const noexcept;
	/// Refuses before writes if consumed/stale; after success no further work is
	/// required for replay. Future world owner must schedule all refusal gates
	/// before its first irreversible write, not mistake this for a world commit.
	bool commit() noexcept;
};

/// Constructs the same replay storage plan on authority or replay receiver.
/// Does not bind a prepared world, send a packet or change dispatch policy.
DLL_LINKAGE std::unique_ptr<PreparedReplayAppend> prepareBattleReplayAppend(
	CGameState & gs, const BattleUnitsChanged & pack);
