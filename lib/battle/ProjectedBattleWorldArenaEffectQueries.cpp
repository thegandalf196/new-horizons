/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"

battle::CUnitState & battle::ProjectedBattleWorldArena::effectQueryStateAt(size_t occurrence)
{
	impl->requireUsable();
	return *impl->slots.at(occurrence)->state; // Existing exact bound info/provider/env are retained.
}

const BonusList & battle::ProjectedBattleWorldArena::exportedBonusesAt(size_t occurrence) const
{
	impl->requireUsable();
	return impl->graph->values(*impl->slots.at(occurrence)->original).exported;
}

void battle::ProjectedBattleWorldArena::refreshExportedBonusesAt(size_t occurrence, const Bonus & value)
{
	impl->requireUsable();
	try { impl->graph->refreshExported(*impl->slots.at(occurrence)->original, value); }
	catch(...) { impl->abandon(); throw; }
}
