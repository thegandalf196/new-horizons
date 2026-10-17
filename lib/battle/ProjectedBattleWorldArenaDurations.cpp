/*
 * ProjectedBattleWorldArenaDurations.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"

std::vector<ProjectedDurationChange> battle::ProjectedBattleWorldArena::reduceExportedDurationsAt(
	size_t occurrence, const CSelector & selector)
{
	impl->requireUsable();
	try { return impl->graph->reduceDurationsRecording(*impl->slots.at(occurrence)->original, selector); }
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::replayDurationChanges(const std::vector<ProjectedDurationChange> & changes)
{
	impl->requireUsable();
	try { impl->graph->replayDurationChanges(changes); }
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::copyEnvironmentValues(const ProjectedUnitEnvironmentState & values)
{
	impl->requireUsable();
	try { impl->sideValues = values; }
	catch(...) { impl->abandon(); throw; }
}
