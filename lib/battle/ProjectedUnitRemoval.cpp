/*
 * ProjectedUnitRemoval.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedUnitRemoval.h"
#include "CUnitState.h"
#include "../CStack.h"
#include "../bonuses/ProjectedBonusGraph.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include <set>
#include <stdexcept>

battle::ProjectedUnitRemoval::ProjectedUnitRemoval(ProjectedBonusGraph & graph, const ProjectedBonusRoster & roster,
	std::vector<Binding> ordered)
	: graph(graph), roster(roster), ordered(std::move(ordered))
{
	validate();
}

void battle::ProjectedUnitRemoval::validate() const
{
	if(failed || ordered.size() != roster.orderedUnits().size())
		throw std::logic_error("Projected removal requires its complete usable ordered roster");
	for(size_t index = 0; index < ordered.size(); ++index)
	{
		const auto & entry = ordered[index];
		if(!entry.original || !entry.state || roster.orderedUnits()[index] != entry.state)
			throw std::invalid_argument("Projected removal binding/order mismatch");
		graph.requireBoundUnit(*entry.original, *entry.state);
	}
}

battle::ProjectedUnitRemoval::Binding * battle::ProjectedUnitRemoval::find(uint32_t id)
{
	const auto it = std::find_if(ordered.begin(), ordered.end(), [id](const Binding & entry)
	{
		return entry.state->unitId() == id;
	});
	return it == ordered.end() ? nullptr : &*it;
}

battle::ProjectedUnitRemoval::Result battle::ProjectedUnitRemoval::removeValuesAndEdges(uint32_t id)
{
	validate();
	try
	{
		std::set<uint32_t> ids;
		ids.insert(id);
		while(!ids.empty())
		{
			const auto toRemoveId = *ids.begin();
			auto * toRemove = find(toRemoveId);
			if(!toRemove)
				return {false, toRemoveId}; // preserve the live early-return boundary
			if(!toRemove->state->ghost)
			{
				toRemove->state->onRemoved(); // synchronous form hooks precede detach
				graph.detachFromAll(*toRemove->original);
				if(toRemove->state->cloneID >= 0)
				{
					ids.insert(toRemove->state->cloneID);
					toRemove->state->cloneID = -1;
				}
				for(auto & entry : ordered)
					if(entry.state->cloneID == toRemoveId)
						entry.state->cloneID = -1;
			}
			ids.erase(toRemoveId);
		}
		return {true, 0};
	}
	catch(...)
	{
		failed = true;
		graph.abandonPreparation();
		throw;
	}
}
