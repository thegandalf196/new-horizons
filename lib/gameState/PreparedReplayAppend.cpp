/*
 * PreparedReplayAppend.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PreparedReplayAppend.h"
#include "PreparedReplaySnapshot.h"
#include "CGameState.h"
#include "../networkPacks/PacksForClientBattle.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

PreparedReplayAppend::PreparedReplayAppend(ReplayLog & log,
	const BattleUnitsChanged & pack, uint32_t day, const SnapshotProvider & snapshot)
	: owner(log), configured(log.recordingPacks), recording(configured), opensChapter(log.chapters.empty())
{
	owner.validateAppendState();
	if(!recording)
		return;
	if(!log.preparationStamp)
		log.preparationStamp = std::make_shared<const unsigned char>(0);
	stamp = log.preparationStamp;
	if(opensChapter)
	{
		// Empty-log copy only: preserve legacy retention, turn and byte semantics
		// without copying recorded history or publishing a chapter before success.
		ReplayLog staged = log;
		staged.beginDay(day, snapshot());
		staged.addPack(ReplayPackSerializer::write(pack));
		firstChapters = std::move(staged.chapters);
	}
	else
	{
		encoded.push_back(ReplayPackSerializer::write(pack));
		reservePacks(encoded.size());
	}
	if(!readyToCommit())
		throw std::runtime_error("Replay log changed during preparation");
}

bool PreparedReplayAppend::readyToCommit() const noexcept
{
	if(consumed || configured != owner.recordingPacks)
		return false;
	return !recording || (stamp && stamp == owner.preparationStamp);
}

bool PreparedReplayAppend::commit() noexcept
{
	if(!readyToCommit())
		return false;
	exchangePreparedStorage();
	return true;
}

void PreparedReplayAppend::exchangePreparedStorage() noexcept
{
	if(recording)
	{
		owner.preparationStamp.swap(retiredStamp); // retain the displaced reservation until retirement
		if(opensChapter)
		{
			static_assert(noexcept(owner.chapters.swap(firstChapters)));
			owner.chapters.swap(firstChapters);
		}
		else
		{
			static_assert(std::is_nothrow_move_constructible_v<std::vector<std::byte>>);
			// Constructor reserved this slot; exclusive ownership forbids changes
			// to capacity/size except through invalidating ReplayLog operations.
			static_assert(std::is_nothrow_copy_constructible_v<ESerializationVersion>);
			for(auto & record : encoded)
			{
				owner.chapters.back().packs.emplace_back(std::move(record.bytes));
				owner.chapters.back().packVersions.emplace_back(record.version);
			}
		}
	}
	consumed = true;
}

std::unique_ptr<PreparedReplayAppend> prepareBattleReplayAppend(
	CGameState & gs, const BattleUnitsChanged & pack)
{
	auto capturedSnapshot = capturePreparedReplaySnapshot(gs, gs.replayLog.isRecordingPacks());

	auto snapshot = [&capturedSnapshot]()
	{
		return std::move(capturedSnapshot);
	};
	return std::make_unique<PreparedReplayAppend>(gs.replayLog, pack, gs.day, snapshot);
}
