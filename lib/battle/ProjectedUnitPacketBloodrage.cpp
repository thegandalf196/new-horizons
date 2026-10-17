/*
 * ProjectedUnitPacketBloodrage.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "NewHorizonsBloodrage.h"
#include "../CCreatureHandler.h"

void battle::ProjectedBattleWorldArena::Data::recordBloodrageDeath(uint32_t unitId)
{
	if(sideValues.inputs(BattleSide::ATTACKER).bloodrageRank == 0
		&& sideValues.inputs(BattleSide::DEFENDER).bloodrageRank == 0)
		return;
	if(!destroyedUnits.insert(unitId).second)
		return;
	const auto * slot = find(unitId, false);
	const auto category = slot
		? categories.lookup(slot->state->unitType()->getJsonKey()) : std::nullopt;
	const bool eliteOrChampion = category
		&& category->category != newHorizonsCreatures::CreatureCategory::CORE;
	bool firstBloodSelected = false;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & state = sideValues.state(side);
		const auto & inputs = sideValues.inputs(side);
		const auto grant = newHorizonsBloodrage::deathGrant(sideHeroes.at(side), !firstBlood,
			eliteOrChampion, inputs.bloodrageRank, inputs.bloodrageCap, state.bloodrageDamagePercent);
		firstBloodSelected |= grant.firstBloodSelected;
		state.bloodrageDamagePercent = grant.damagePercent;
	}
	firstBlood |= firstBloodSelected;
}

const std::set<uint32_t> & battle::ProjectedBattleWorldArena::bloodrageDeaths() const
{
	impl->requireUsable();
	return impl->destroyedUnits;
}
bool battle::ProjectedBattleWorldArena::firstBloodUsed() const
{
	impl->requireUsable();
	return impl->firstBlood;
}
int battle::ProjectedBattleWorldArena::bloodrageDamage(BattleSide side) const
{
	impl->requireUsable();
	return impl->sideValues.state(side).bloodrageDamagePercent;
}
