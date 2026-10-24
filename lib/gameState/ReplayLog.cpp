/*
 * ReplayLog.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ReplayLog.h"

#include "CGameState.h"

#include "../networkPacks/NetPacksBase.h"
#include "../networkPacks/PacksForClient.h"
#include "../serializer/CMemorySerializer.h"

#include <type_traits>
#include <utility>

ReplayLog::ReplayLog(const ReplayLog & other)
	: chapters(other.chapters), recordEntireGame(other.recordEntireGame),
	  roundsKept(other.roundsKept), recordingPacks(other.recordingPacks)
{
}

ReplayLog & ReplayLog::operator=(const ReplayLog & other)
{
	if(this != &other)
	{
		preparationStamp.reset();
		chapters = other.chapters;
		recordEntireGame = other.recordEntireGame;
		roundsKept = other.roundsKept;
		recordingPacks = other.recordingPacks;
	}
	return *this;
}

ReplayLog::ReplayLog(ReplayLog && other) noexcept
	: chapters(std::move(other.chapters)), recordEntireGame(other.recordEntireGame),
	  roundsKept(other.roundsKept), recordingPacks(other.recordingPacks)
{
	static_assert(std::is_nothrow_move_constructible_v<decltype(chapters)>);
	other.preparationStamp.reset();
}

ReplayLog & ReplayLog::operator=(ReplayLog && other) noexcept
{
	if(this != &other)
	{
		static_assert(std::is_nothrow_move_assignable_v<decltype(chapters)>);
		preparationStamp.reset();
		other.preparationStamp.reset();
		chapters = std::move(other.chapters);
		recordEntireGame = other.recordEntireGame;
		roundsKept = other.roundsKept;
		recordingPacks = other.recordingPacks;
	}
	return *this;
}

void ReplayLog::configure(bool recordEntireGameValue, uint32_t roundsKeptValue)
{
	preparationStamp.reset();
	recordEntireGame = recordEntireGameValue;
	roundsKept = roundsKeptValue;
	recordingPacks = true;
}

void ReplayLog::reconfigureOnLoad(uint32_t roundsKeptValue)
{
	preparationStamp.reset();
	roundsKept = roundsKeptValue;
	recordingPacks = true;
}

void ReplayLog::beginDay(uint32_t day, ReplayPayload snapshot)
{
	validateAppendState();
	replayPayload::validateStoredVersion(snapshot.version);
	preparationStamp.reset();
	logGlobal->debug("Replay: new chapter, snapshot of %d bytes, %d chapters kept", snapshot.bytes.size(), chapters.size() + 1);

	chapters.emplace_back();
	chapters.back().day = day;
	chapters.back().snapshotVersion = snapshot.bytes.empty() ? ESerializationVersion::NONE : snapshot.version;
	chapters.back().gamestateSnapshot = std::move(snapshot.bytes);

	dropExpiredData();
}

void ReplayLog::dropExpiredData()
{
	// the current day plus `roundsKept` days before it stay fully replayable
	const size_t chaptersToKeep = roundsKept + 1;

	if(chapters.size() <= chaptersToKeep)
		return;

	if(!recordEntireGame)
	{
		chapters.erase(chapters.begin(), chapters.end() - chaptersToKeep);
		return;
	}

	// when the whole game is recorded no packs may be lost, but snapshots of days that fell out
	// of the window are not needed - those days are reached by fast-forwarding from the first one
	for(size_t i = 1; i < chapters.size() - chaptersToKeep; ++i)
	{
		chapters[i].gamestateSnapshot.clear();
		chapters[i].snapshotVersion = ESerializationVersion::NONE;
		chapters[i].gamestateSnapshot.shrink_to_fit();
	}
}

void ReplayLog::addTurn(const PlayerColor & player)
{
	preparationStamp.reset();
	if(chapters.empty())
		return;

	// the server re-announces a turn that is already running, e.g. after loading a save
	for(const auto & turn : chapters.back().turns)
		if(turn.player == player && turn.lastPack == ReplayTurnMark::ongoingTurn)
			return;

	ReplayTurnMark mark;
	mark.player = player;
	mark.firstPack = static_cast<uint32_t>(chapters.back().packs.size());

	chapters.back().turns.push_back(mark);
}

void ReplayLog::endTurn(const PlayerColor & player)
{
	preparationStamp.reset();
	if(chapters.empty())
		return;

	// with simturns several turns are open at once, so the right one has to be picked
	auto & turns = chapters.back().turns;
	for(size_t i = turns.size(); i-- > 0;)
	{
		if(turns[i].player == player && turns[i].lastPack == ReplayTurnMark::ongoingTurn)
		{
			turns[i].lastPack = static_cast<uint32_t>(chapters.back().packs.size());
			return;
		}
	}
}

void ReplayLog::recordPack(CPackForClient & pack, CGameState & gs)
{
	// every game day opens a new chapter, anchored by the state that day started from.
	// NewTurn is recorded before it is applied, so the new day is only known from the pack itself
	const auto * newTurn = dynamic_cast<const NewTurn *>(&pack);
	if(chapters.empty() || newTurn != nullptr)
		beginDay(newTurn ? newTurn->day : gs.day, gs.saveReplaySnapshot());

	if(const auto * turnStart = dynamic_cast<const PlayerStartsTurn *>(&pack))
		addTurn(turnStart->player);

	if(const auto * turnEnd = dynamic_cast<const PlayerEndsTurn *>(&pack))
		endTurn(turnEnd->player);

	addPack(ReplayPackSerializer::write(pack));
}

void ReplayLog::addPack(ReplayPayload payload)
{
	validateAppendState();
	replayPayload::validateStoredVersion(payload.version);
	preparationStamp.reset();
	if(chapters.empty())
		return;
	auto & chapter = chapters.back();
	if(chapter.packs.size() == chapter.packs.max_size() || chapter.packVersions.size() == chapter.packVersions.max_size())
		throw std::length_error("Replay packet count exceeds container limits");
	const auto required = chapter.packs.size() + 1;
	if(required > chapter.packs.capacity())
	{
		const auto growth = std::min(std::max(chapter.packs.size(), size_t{1}), chapter.packs.max_size() - chapter.packs.size());
		chapter.packs.reserve(std::max(required, chapter.packs.size() + growth));
	}
	chapter.packVersions.reserve(std::min(chapter.packs.capacity(), chapter.packVersions.max_size()));
	static_assert(std::is_nothrow_move_constructible_v<std::vector<std::byte>>);
	static_assert(std::is_nothrow_copy_constructible_v<ESerializationVersion>);
	chapter.packs.push_back(std::move(payload.bytes));
	chapter.packVersions.push_back(payload.version);
}

bool ReplayLog::isRecordingPacks() const
{
	return recordingPacks;
}

bool ReplayLog::empty() const
{
	return chapters.empty();
}

bool ReplayLog::isRecordingEntireGame() const
{
	return recordEntireGame;
}

bool ReplayLog::canReplayEntireGame() const
{
	validateStorage();
	if(!recordEntireGame || chapters.empty() || chapters.front().gamestateSnapshot.empty()
		|| chapters.front().snapshotVersion == ESerializationVersion::NONE)
		return false;
	for(const auto & chapter : chapters)
		for(const auto version : chapter.packVersions)
			if(version == ESerializationVersion::NONE)
				return false;
	return true;
}

const std::vector<ReplayChapter> & ReplayLog::getChapters() const
{
	return chapters;
}
