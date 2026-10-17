/*
 * PreparedReplayBatch.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreparedReplayAppend.h"
#include <algorithm>
#include <stdexcept>

void PreparedReplayAppend::reservePacks(size_t count)
{
	auto & packs = owner.chapters.back().packs;
	if(count > packs.max_size() - packs.size())
		throw std::length_error("Replay pack vector is full");
	const auto required = packs.size() + count;
	if(packs.capacity() < required)
	{
		const auto growth = std::min(std::max(packs.size(), size_t{1}), packs.max_size() - packs.size());
		packs.reserve(std::max(required, packs.size() + growth));
	}
}

PreparedReplayAppend::PreparedReplayAppend(ReplayLog & log, Records records, uint32_t day,
	const SnapshotProvider & snapshot, RecordingMode mode)
	: owner(log), encoded(std::move(records)), configured(log.recordingPacks),
	  recording((mode == RecordingMode::UNCONDITIONAL || configured) && !encoded.empty()),
	  opensChapter(log.chapters.empty())
{
	if(!recording)
		return;
	if(!log.preparationStamp)
		log.preparationStamp = std::make_shared<const unsigned char>(0);
	stamp = log.preparationStamp;
	if(opensChapter)
	{
		ReplayLog staged = log;
		staged.beginDay(day, snapshot());
		for(auto & bytes : encoded)
			staged.addPack(std::move(bytes));
		firstChapters = std::move(staged.chapters);
	}
	else
		reservePacks(encoded.size());
	if(!readyToCommit())
		throw std::runtime_error("Replay log changed during preparation");
}
