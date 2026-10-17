/*
 * ProjectedUnitPacketProgram.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "../networkPacks/BattleChanges.h"
#include <stdexcept>

void battle::ProjectedBattleWorldArena::applyUnitChanges(const std::vector<UnitChanges> & changes)
{
	impl->requireUsable();
	try
	{
		const auto candidates = impl->captureCandidates(changes);
		impl->applyEntries(*this, changes);
		impl->finishCandidates(candidates, changes);
	}
	catch(...)
	{
		impl->abandon();
		throw;
	}
}
