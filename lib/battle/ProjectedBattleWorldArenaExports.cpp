/*
 * ProjectedBattleWorldArenaExports.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"

void battle::ProjectedBattleWorldArena::addExportedBonusAt(size_t occurrence,
	const std::shared_ptr<Bonus> & record)
{
	impl->requireUsable();
	try { impl->graph->addExported(*impl->slots.at(occurrence)->original, record); }
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::removeExportedBonusAt(size_t occurrence,
	const std::shared_ptr<Bonus> & record)
{
	impl->requireUsable();
	try { impl->graph->removeExported(*impl->slots.at(occurrence)->original, record); }
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::expireExportedBonusesAt(size_t occurrence, const CSelector & selector)
{
	impl->requireUsable();
	try { impl->graph->removeBonusesRecursive(*impl->slots.at(occurrence)->original, selector); }
	catch(...) { impl->abandon(); throw; }
}
