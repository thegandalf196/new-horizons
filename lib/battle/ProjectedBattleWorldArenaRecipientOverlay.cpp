/*
 * ProjectedBattleWorldArenaRecipientOverlay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"

void battle::ProjectedBattleWorldArena::installRecipientOverlaysAt(size_t occurrence,
	std::vector<recipientBonusOverlay::Layer> layers)
{
	impl->requireUsable();
	try { impl->graph->installRecipientOverlays(*impl->slots.at(occurrence)->original, std::move(layers)); }
	catch(...) { impl->abandon(); throw; }
}

bool battle::ProjectedBattleWorldArena::hasUnresolvedRecipientOverlays() const
{
	impl->requireUsable();
	return impl->graph->hasUnresolvedRecipientOverlays();
}

std::optional<std::vector<recipientBonusOverlay::Layer>> battle::ProjectedBattleWorldArena::removeRecipientOverlaysMatchingAt(
	size_t occurrence, const CSelector & selector)
{
	impl->requireUsable();
	try { return impl->graph->removeRecipientOverlaysMatching(*impl->slots.at(occurrence)->original, selector); }
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::replayRecipientOverlaysAt(size_t occurrence,
	std::vector<recipientBonusOverlay::Layer> layers)
{
	impl->requireUsable();
	try { impl->graph->replayRecipientOverlays(*impl->slots.at(occurrence)->original, std::move(layers)); }
	catch(...) { impl->abandon(); throw; }
}
