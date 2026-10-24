/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/StdInc.h"
#include "SaveParityEvidence.h"
#include <algorithm>
#include <limits>

namespace saveParity
{
namespace
{
const ReplayChapter & chapterAt(const Evidence & evidence, uint32_t day)
{
	if(evidence.chapters.empty() || evidence.chapters.back().day != evidence.day)
		throw std::runtime_error("Replay does not end at the inspected world day");
	const ReplayChapter * found = nullptr;
	uint32_t prior = 0;
	bool hasPrior = false;
	for(const auto & chapter : evidence.chapters)
	{
		if(hasPrior && chapter.day <= prior)
			throw std::runtime_error("Replay chapter days must be strictly ordered");
		prior = chapter.day;
		hasPrior = true;
		if(chapter.day == day)
			found = &chapter;
		for(const auto & mark : chapter.turns)
			if(mark.firstPack > chapter.packs.size()
				|| (mark.lastPack != ReplayTurnMark::ongoingTurn
					&& (mark.lastPack < mark.firstPack || mark.lastPack > chapter.packs.size())))
				throw std::runtime_error("Replay turn marks are outside their packet range");
	}
	if(!found || found->gamestateSnapshot.empty())
		throw std::runtime_error("Required replay chapter/snapshot is missing or expired");
	return *found;
}
}

void requireTurnCoverage(const Evidence & start, const Evidence & finish)
{
	if(start.day == std::numeric_limits<uint32_t>::max() || finish.day != start.day + 1
		|| start.actingPlayers.size() != 1 || finish.actingPlayers != start.actingPlayers)
		throw std::runtime_error("Inspection requires exactly one day and the same sole active player");
	const auto & before = chapterAt(start, start.day);
	const auto & after = chapterAt(finish, start.day);
	const auto & next = chapterAt(finish, finish.day);
	if(before.snapshotVersion != after.snapshotVersion
		|| before.packVersions.size() > after.packVersions.size()
		|| !std::equal(before.packVersions.begin(), before.packVersions.end(), after.packVersions.begin())
		|| before.gamestateSnapshot != after.gamestateSnapshot
		|| before.packs.size() > after.packs.size()
		|| !std::equal(before.packs.begin(), before.packs.end(), after.packs.begin())
		|| before.turns.size() > after.turns.size())
		throw std::runtime_error("Post-turn replay does not retain the exact starting prefix");
	bool completed = false;
	for(size_t index = 0; index < before.turns.size(); ++index)
	{
		const auto & old = before.turns[index];
		const auto & current = after.turns[index];
		if(old.player != current.player || old.firstPack != current.firstPack
			|| (old.lastPack != ReplayTurnMark::ongoingTurn && old.lastPack != current.lastPack))
			throw std::runtime_error("Starting replay turn identity changed");
		if(old.player == *start.actingPlayers.begin() && old.lastPack == ReplayTurnMark::ongoingTurn)
			completed = current.lastPack != ReplayTurnMark::ongoingTurn;
	}
	bool resumed = false;
	for(const auto & mark : next.turns)
		if(mark.player == *start.actingPlayers.begin() && mark.lastPack == ReplayTurnMark::ongoingTurn)
			resumed = true;
	if(!completed || !resumed || after.packs.size() == before.packs.size() || next.packs.empty())
		throw std::runtime_error("Replay does not cover the complete end-turn and next human turn");
}
}
