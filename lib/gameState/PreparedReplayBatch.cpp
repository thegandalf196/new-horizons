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
	auto & chapter = owner.chapters.back();
	if(count > chapter.packs.max_size() - chapter.packs.size()
		|| count > chapter.packVersions.max_size() - chapter.packVersions.size())
		throw std::length_error("Prepared replay count exceeds container limits");
	const auto required = chapter.packs.size() + count;
	if(required > chapter.packs.capacity())
	{
		const auto growth = std::min(std::max(chapter.packs.size(), size_t{1}), chapter.packs.max_size() - chapter.packs.size());
		chapter.packs.reserve(std::max(required, chapter.packs.size() + growth));
	}
	// Both reserves precede publication. Failure may change capacity, never
	// logical bytes/versions; exclusive ownership invalidates stale plans.
	chapter.packVersions.reserve(std::min(chapter.packs.capacity(), chapter.packVersions.max_size()));
}

PreparedReplayAppend::PreparedReplayAppend(ReplayLog & log, Records records, uint32_t day,
	const SnapshotProvider & snapshot, RecordingMode mode)
	: owner(log), encoded(std::move(records)), configured(log.recordingPacks),
	  recording((mode == RecordingMode::UNCONDITIONAL || configured) && !encoded.empty()),
	  opensChapter(log.chapters.empty())
{
	owner.validateAppendState();
	for(const auto & record : encoded)
		replayPayload::validateStoredVersion(record.version);
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
