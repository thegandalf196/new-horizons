/*
 * ReplayPlanner.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../StdInc.h"
#include "GameplayReplayer.h"
#include "../../lib/gameState/ReplayLog.h"
#include <algorithm>
#include <stdexcept>

void ReplaySequence::validate() const
{
	replayPayload::requireKnownVersion(snapshot.version);
	for(const auto & pack : fastForwardPacks)
		replayPayload::requireKnownVersion(pack.version);
	for(const auto & day : days)
		for(const auto & pack : day.packs)
			replayPayload::requireKnownVersion(pack.version);
}

/// index of the newest chapter at or before `chapter` that still carries a snapshot
static size_t findAnchorChapter(const std::vector<ReplayChapter> & chapters, size_t chapter)
{
	for(size_t i = chapter + 1; i-- > 0;)
		if(!chapters[i].gamestateSnapshot.empty() && chapters[i].snapshotVersion != ESerializationVersion::NONE)
			return i;

	throw std::runtime_error("Recording has no known-format gamestate to start this replay from!");
}

/// index one past the last pack of this turn, resolved against the day it belongs to
static size_t turnLastPack(const ReplayTurnMark & turn, const ReplayChapter & chapter)
{
	if(turn.lastPack == ReplayTurnMark::ongoingTurn)
		return chapter.packs.size();

	return std::min<size_t>(turn.lastPack, chapter.packs.size());
}

/// True if the turns of this day overlap, which is the case with simultaneous turns
static bool hasInterleavedTurns(const ReplayChapter & chapter)
{
	for(size_t i = 1; i < chapter.turns.size(); ++i)
		if(chapter.turns[i].firstPack < chapter.turns[i - 1].lastPack)
			return true;

	return false;
}

std::vector<ReplayTurnOption> ReplayPlanner::availableTurns(const ReplayLog & log)
{
	std::vector<ReplayTurnOption> result;
	log.validateStorage();
	const auto & chapters = log.getChapters();

	for(size_t chapterIndex = 0; chapterIndex < chapters.size(); ++chapterIndex)
	{
		const auto & chapter = chapters[chapterIndex];
		const bool simultaneous = hasInterleavedTurns(chapter);

		for(size_t turnIndex = 0; turnIndex < chapter.turns.size(); ++turnIndex)
		{
			const auto & turn = chapter.turns[turnIndex];
			const bool ongoing = turn.lastPack == ReplayTurnMark::ongoingTurn;

			// such a day is replayed from its start for every player, so a player that did nothing
			// would only offer a second look at the very same packs
			if(simultaneous && turn.firstPack >= turnLastPack(turn, chapter))
				continue;

			result.push_back({turn.player, chapter.day, ongoing, chapterIndex, turnIndex});
		}
	}

	return result;
}

ReplaySequence ReplayPlanner::prepareTurn(const ReplayLog & log, const ReplayTurnOption & option)
{
	log.validateStorage();
	const auto & chapters = log.getChapters();

	if(option.chapter >= chapters.size() || option.turn >= chapters[option.chapter].turns.size())
		throw std::runtime_error("Requested replay of a turn that is no longer available!");

	const auto & chapter = chapters[option.chapter];
	const auto & turn = chapter.turns[option.turn];
	const size_t anchor = findAnchorChapter(chapters, option.chapter);

	// with simturns the packs of a day belong to no single player, so the day is replayed from its
	// start and ends with the last pack of this player. Otherwise a turn spans start to end.
	const bool simultaneous = hasInterleavedTurns(chapter);
	const size_t firstPack = simultaneous ? 0 : turn.firstPack;
	const size_t lastPack = turnLastPack(turn, chapter);

	ReplaySequence result;
	result.replayedPlayer = turn.player;
	result.day = chapter.day;
	result.snapshot = chapters[anchor].snapshotPayload();

	// everything between the anchor and the replayed turn is applied without any visuals
	for(size_t i = anchor; i < option.chapter; ++i)
		for(size_t pack = 0; pack < chapters[i].packs.size(); ++pack)
			result.fastForwardPacks.push_back(chapters[i].packPayload(pack));
	for(size_t pack = 0; pack < firstPack; ++pack)
		result.fastForwardPacks.push_back(chapter.packPayload(pack));

	ReplayDay day;
	for(size_t pack = firstPack; pack < lastPack; ++pack)
		day.packs.push_back(chapter.packPayload(pack));
	day.passes.push_back({simultaneous ? std::make_optional(turn.player) : std::nullopt, day.packs.size()});
	result.days.push_back(std::move(day));

	result.validate();
	return result;
}

ReplaySequence ReplayPlanner::prepareEntireGame(const ReplayLog & log)
{
	if(!log.canReplayEntireGame())
		throw std::runtime_error("This game was not recorded from its beginning!");

	log.validateStorage();
	const auto & chapters = log.getChapters();

	ReplaySequence result;
	result.snapshot = chapters.front().snapshotPayload();

	// the first chapter holds the packs sent before any turn was announced, so it may carry no turn
	for(const auto & chapter : chapters)
	{
		if(chapter.turns.empty())
			continue;

		result.replayedPlayer = chapter.turns.front().player;
		result.day = chapter.day;
		break;
	}

	for(const auto & chapter : chapters)
	{
		ReplayDay day;
		for(size_t pack = 0; pack < chapter.packs.size(); ++pack)
			day.packs.push_back(chapter.packPayload(pack));

		// a day of simultaneous turns is watched once per player, shortest pass first - every pass
		// ends with the last pack of its player, and only the camera tells the players apart
		if(hasInterleavedTurns(chapter))
		{
			for(const auto & turn : chapter.turns)
				if(turn.firstPack < turnLastPack(turn, chapter))
					day.passes.push_back({turn.player, turnLastPack(turn, chapter)});

			std::sort(day.passes.begin(), day.passes.end(),
				[](const ReplayPass & left, const ReplayPass & right) { return left.packCount < right.packCount; });
		}

		// the last pass shows what is left of the day, so the state is complete for the next one
		if(day.passes.empty())
			day.passes.push_back({std::nullopt, day.packs.size()});
		else
			day.passes.back().packCount = day.packs.size();

		result.days.push_back(std::move(day));
	}

	result.validate();
	return result;
}
