/*
 * PreparedReplayAppend.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PreparedReplayAppend.h"
#include "CGameState.h"
#include "../networkPacks/PacksForClientBattle.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

PreparedReplayAppend::PreparedReplayAppend(ReplayLog & log,
	const BattleUnitsChanged & pack, uint32_t day, const SnapshotProvider & snapshot)
	: owner(log), recording(log.recordingPacks), opensChapter(log.chapters.empty())
{
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
		encoded = ReplayPackSerializer::write(pack);
		auto & packs = log.chapters.back().packs;
		if(packs.size() == packs.max_size())
			throw std::length_error("Replay pack vector is full");
		if(packs.capacity() == packs.size())
		{
			const auto growth = std::min(std::max(packs.size(), size_t{1}), packs.max_size() - packs.size());
			packs.reserve(packs.size() + growth);
		}
	}
	if(!readyToCommit())
		throw std::runtime_error("Replay log changed during preparation");
}

bool PreparedReplayAppend::readyToCommit() const noexcept
{
	if(consumed || recording != owner.recordingPacks)
		return false;
	return !recording || (stamp && stamp == owner.preparationStamp);
}

bool PreparedReplayAppend::commit() noexcept
{
	if(!readyToCommit())
		return false;
	if(recording)
	{
		owner.preparationStamp.reset();
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
			owner.chapters.back().packs.emplace_back(std::move(encoded));
		}
	}
	consumed = true;
	return true;
}

std::unique_ptr<PreparedReplayAppend> prepareBattleReplayAppend(
	CGameState & gs, const BattleUnitsChanged & pack)
{
	std::vector<std::byte> capturedSnapshot;
	if(gs.replayLog.isRecordingPacks() && gs.replayLog.empty())
	{
		// Complete the legacy log swap roundtrip before acquiring this plan's
		// reservation identity. Any older outstanding plans remain invalidated.
		ReplayLog preserved = gs.replayLog;
		try
		{
			capturedSnapshot = gs.saveToMemory();
		}
		catch(...)
		{
			static_assert(std::is_nothrow_move_assignable_v<ReplayLog>);
			gs.replayLog = std::move(preserved);
			throw;
		}
	}
	auto snapshot = [&capturedSnapshot]()
	{
		return std::move(capturedSnapshot);
	};
	return std::make_unique<PreparedReplayAppend>(gs.replayLog, pack, gs.day, snapshot);
}
