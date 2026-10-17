/*
 * ProjectedBattleWorldArenaForecast.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "UnitStateValueFields.h"
#include "../mapObjects/CGHeroInstance.h"
#include <stdexcept>

JsonNode battle::ProjectedBattleWorldArena::damagePhysicalAndSnapshotAt(size_t occurrence, int64_t & damage)
{
	impl->requireUsable();
	try
	{
		auto & state = *impl->slots.at(occurrence)->state;
		state.damage(damage, false, DamageProvenance::PHYSICAL_CREATURE);
		return state.save();
	}
	catch(...) { impl->abandon(); throw; }
}

TConstBonusListPtr battle::ProjectedBattleWorldArena::projectedHeroBonuses(BattleSide side, bool unstacked) const
{
	impl->requireUsable();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Projected hero query requires a fighting side");
	const auto * hero = impl->heroes.at(side);
	if(!hero)
		return std::make_shared<BonusList>();
	return impl->graph->query(*hero, Selector::all, unstacked);
}

std::vector<ProjectedExportRemoval> battle::ProjectedBattleWorldArena::removeExportedBonusesRecordingAt(
	size_t occurrence, const CSelector & selector)
{
	impl->requireUsable();
	try { return impl->graph->removeBonusesRecursiveRecording(*impl->slots.at(occurrence)->original, selector); }
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::replayExportedRemovals(const std::vector<ProjectedExportRemoval> & receipts)
{
	impl->requireUsable();
	try
	{
		for(const auto & receipt : receipts)
			impl->graph->removeExported(*receipt.source, receipt.record);
	}
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::copyQueryValuesAt(size_t occurrence, const CUnitState & state)
{
	impl->requireUsable();
	try
	{
		auto & slot = *impl->slots.at(occurrence);
		if(slot.state->unitId() != state.unitId() || slot.state->unitType() != state.unitType()
			|| slot.state->unitSide() != state.unitSide() || slot.state->unitOwner() != state.unitOwner())
			throw std::invalid_argument("Forecast query values require the same physical descriptor");
		UnitStateValueFields::copy(*slot.state, state);
		// Graph query providers are explicitly non-cacheable. Scalar query sync
		// is NOT a native graph invalidation event; synchronize only a real form
		// source transition in the canonical owner before its next query.
		impl->graph->synchronizeForm(*slot.original);
	}
	catch(...) { impl->abandon(); throw; }
}

void battle::ProjectedBattleWorldArena::restoreQuerySnapshotAt(size_t occurrence, const JsonNode & state)
{
	impl->requireUsable();
	try
	{
		auto & slot = *impl->slots.at(occurrence);
		impl->graph->loadBoundState(*slot.original, *slot.state, state);
		impl->graph->nodeHasChanged(*slot.original);
	}
	catch(...) { impl->abandon(); throw; }
}
