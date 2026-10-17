/*
 * ProjectedBattleWorldArenaGraph.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include <stdexcept>

std::vector<battle::ProjectedUnitRemoval::Binding> battle::ProjectedBattleWorldArena::Data::removalBindings() const
{
	std::vector<ProjectedUnitRemoval::Binding> result;
	result.reserve(slots.size());
	for(const auto & slot : slots)
		result.push_back({slot->original, slot->state.get()});
	return result;
}

battle::ProjectedBattleWorldArena::UpdateDecision battle::ProjectedBattleWorldArena::classifyUpdate(uint32_t id, int64_t delta)
{
	impl->requireUsable();
	try
	{
		const auto * slot = impl->find(id, false);
		if(!slot)
			throw std::runtime_error("Invalid unit id in BattleInfo update");
		const auto gate = classifyUnitUpdateGate(*slot->state, delta);
		if(gate == UnitUpdateGate::IGNORE_HEALTH_STOP || gate == UnitUpdateGate::IGNORE_STATE_STOP)
			return UpdateDecision::IGNORE_TIME_STOP;
		if(gate == UnitUpdateGate::CHECK_RESURRECTION_SPACE
			&& !accessibility().accessible(slot->state->getPosition(), slot->state.get()))
			return UpdateDecision::IGNORE_OCCUPIED_RESURRECTION;
		return UpdateDecision::APPLY;
	}
	catch(...) { impl->abandon(); throw; }
}

battle::ProjectedUnitRemoval::Result battle::ProjectedBattleWorldArena::removeValuesAndEdges(uint32_t id)
{
	impl->requireUsable();
	try
	{
		ProjectedUnitRemoval removal(*impl->graph, impl->roster, impl->removalBindings());
		return removal.removeValuesAndEdges(id);
	}
	catch(...) { impl->abandon(); throw; }
}

TCNodesVector battle::ProjectedBattleWorldArena::capturedGraphNodes() const
{
	impl->requireUsable();
	return impl->graph->capturedNodes();
}

TNodesVector battle::ProjectedBattleWorldArena::ownedGraphNodes() const
{
	impl->requireUsable();
	TNodesVector result;
	for(const auto & slot : impl->slots)
		if(slot->owned)
			result.push_back(slot->owned.get());
	for(const auto & query : impl->queries)
		result.push_back(&query->identity());
	return result;
}

void battle::ProjectedBattleWorldArena::prepareGraphStorage(const TNodesVector & authority)
{
	impl->requireUsable();
	try { impl->finalizer = std::make_unique<PreparedBonusGraphFinalization>(*impl->graph, authority); }
	catch(...) { impl->abandon(); throw; }
}

bool battle::ProjectedBattleWorldArena::graphStorageReady() const noexcept
{
	return impl->finalizer && impl->finalizer->readyToCommit();
}
