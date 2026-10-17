/*
 * ProjectedUnitPacketCapture.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "UnitOwnershipQuery.h"
#include "../networkPacks/BattleChanges.h"

namespace
{
class PacketOwnership final : public battle::UnitOwnershipQuery
{
	const battle::ProjectedUnitEnvironmentState & values;
public:
	explicit PacketOwnership(const battle::ProjectedUnitEnvironmentState & values) : values(values) {}
	PlayerColor sidePlayer(BattleSide side) const override { return values.inputs(side).color; }
};
}

battle::ProjectedBattleWorldArena::Data::PacketCandidates
battle::ProjectedBattleWorldArena::Data::captureCandidates(const std::vector<UnitChanges> & changes) const
{
	PacketCandidates result;
	PacketOwnership ownership(sideValues);
	for(const auto & change : changes)
	{
		const auto * slot = find(change.id, false);
		const auto * unit = slot ? slot->state.get() : nullptr;
		if(change.operation != BattleChanges::EOperation::UPDATE || !unit || !unit->alive()
			|| change.healthDelta >= 0)
			continue;
		const auto side = ownership.playerToSide(ownership.actionController(*unit));
		if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			&& newHorizonsCrisisCommand::eligible(sideHeroes.at(side)))
			result.crisis.push_back({side, change.id});
	}
	for(const auto & change : changes)
	{
		const auto * slot = find(change.id, false);
		const auto * unit = slot ? slot->state.get() : nullptr;
		if(change.operation == BattleChanges::EOperation::REMOVE && unit && unit->alive()
			&& !unit->acquireState()->summoned && !unit->isClone())
			result.removed.insert(change.id);
	}
	return result;
}
