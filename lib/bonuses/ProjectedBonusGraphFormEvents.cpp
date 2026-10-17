/*
 * ProjectedBonusGraphFormEvents.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include "../CStack.h"
#include "../battle/CUnitState.h"
#include <stdexcept>

bool ProjectedBonusGraph::hasExactView(const CStack & original, const battle::CUnitState & unit) const noexcept
{
	const auto * detached = dynamic_cast<const battle::CUnitStateDetached *>(&unit);
	const auto found = views.find(&original);
	const auto binding = bindings.find(&original);
	return detached && found != views.end() && found->second
		&& detached->usesBonusProvider(*found->second)
		&& (binding == bindings.end() || !binding->second.occurrence || detached->usesUnitInfo(original));
}

void ProjectedBonusGraph::View::onBattleFormChanged(const battle::CUnitState & unit) const
{
	graph.requireUsable();
	try
	{
		const auto * original = dynamic_cast<const CStack *>(&node);
		const auto found = graph.bindings.find(original);
		if(!original || found == graph.bindings.end() || found->second.unit != &unit
			|| !graph.hasExactView(*original, unit))
			throw std::logic_error("Projected form notification requires its exact bound unit");
		graph.synchronizeForm(*original);
		graph.validate();
		// CStack::onBattleFormChanged ALWAYS calls nodeHasChanged, including
		// unchanged source identity, after detach/attach and footprint refresh.
		graph.invalidations.record(true, original, graph);
	}
	catch(...) { graph.poisoned = true; throw; }
}

void ProjectedBonusGraph::loadBoundState(const CStack & original, battle::CUnitStateDetached & state,
	const JsonNode & data)
{
	requireUsable();
	try
	{
		const auto found = bindings.find(&original);
		if(found == bindings.end() || found->second.unit != &state || !hasExactView(original, state))
			throw std::logic_error("Projected state load requires its exact bound unit");
		validate();
		state.load(data); // original parsing/hook/health order; no normalization
	}
	catch(...) { poisoned = true; throw; }
}
