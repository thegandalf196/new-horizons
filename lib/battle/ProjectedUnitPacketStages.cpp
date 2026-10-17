/*
 * ProjectedUnitPacketStages.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "../networkPacks/BattleChanges.h"
#include <stdexcept>

void battle::ProjectedBattleWorldArena::Data::applyEntries(ProjectedBattleWorldArena & owner,
	const std::vector<UnitChanges> & changes)
{
	for(const auto & change : changes)
	{
		switch(change.operation)
		{
			case BattleChanges::EOperation::UPDATE:
				owner.updateUnit(change.id, change.data, change.healthDelta);
				break;
			case BattleChanges::EOperation::REMOVE:
				owner.removeUnit(change.id);
				break;
			case BattleChanges::EOperation::ADD:
				owner.addUnit(change.id, change.data);
				break;
			default:
				throw std::runtime_error("Unknown unit operation");
		}
	}
}

void battle::ProjectedBattleWorldArena::Data::finishCandidates(const PacketCandidates & candidates,
	const std::vector<UnitChanges> & changes)
{
	for(const auto id : candidates.removed)
		recordBloodrageDeath(id);
	recordCrisisDeaths(candidates.crisis);
	for(const auto & change : changes)
	{
		const auto * slot = find(change.id, false);
		if(slot && slot->state->alive())
			destroyedUnits.erase(change.id);
	}
}
