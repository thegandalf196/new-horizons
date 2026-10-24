/*
 * ReplayProvenance.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ReplayLog.h"
#include <algorithm>
#include <stdexcept>
void ReplayChapter::validateStorage() const
{
	if(packVersions.size() != packs.size())
		throw std::runtime_error("Replay packet provenance count mismatch");
	replayPayload::validateStoredVersion(snapshotVersion);
	if(gamestateSnapshot.empty() && snapshotVersion != ESerializationVersion::NONE)
		throw std::runtime_error("Empty replay snapshot has encoding provenance");
	for(const auto version : packVersions)
		replayPayload::validateStoredVersion(version);
}
void ReplayChapter::validateSerialization(bool provenanceSupported) const
{
	validateStorage();
	if(!provenanceSupported && (snapshotVersion != ESerializationVersion::NONE
		|| std::any_of(packVersions.begin(), packVersions.end(), [](auto version) { return version != ESerializationVersion::NONE; })))
		throw std::runtime_error("Cannot discard replay payload encoding provenance");
}
ReplayPayload ReplayChapter::snapshotPayload() const
{
	validateStorage();
	return {gamestateSnapshot, snapshotVersion};
}
ReplayPayload ReplayChapter::packPayload(size_t index) const
{
	if(packVersions.size() != packs.size())
		throw std::runtime_error("Replay packet provenance count mismatch");
	replayPayload::validateStoredVersion(packVersions.at(index));
	return {packs.at(index), packVersions.at(index)};
}
void ReplayLog::validateStorage() const
{
	for(const auto & chapter : chapters)
		chapter.validateStorage();
}
void ReplayLog::validateSerialization(bool provenanceSupported) const
{
	for(const auto & chapter : chapters)
		chapter.validateSerialization(provenanceSupported);
}

void ReplayLog::validateAppendState() const
{
	// Stored tags were validated at creation/decode and are exposed read-only.
	// Do not rescan the entire history for every recorded packet.
	if(!chapters.empty())
	{
		const auto & chapter = chapters.back();
		if(chapter.packVersions.size() != chapter.packs.size())
			throw std::runtime_error("Replay packet provenance count mismatch");
		replayPayload::validateStoredVersion(chapter.snapshotVersion);
		if(chapter.gamestateSnapshot.empty() && chapter.snapshotVersion != ESerializationVersion::NONE)
			throw std::runtime_error("Empty replay snapshot has encoding provenance");
	}
}
