/*
 * ProjectedBonusInvalidations.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusInvalidations.h"
#include "BonusGraphQuery.h"
#include "PreparedBonusGraphStorage.h"
#include <stdexcept>

void ProjectedBonusInvalidations::record(bool advance, const CBonusSystemNode * root, const BonusGraphQuery & graph)
{
	const auto observed = PreparedBonusGraphStorage::currentGeneration();
	if(!initialGeneration)
	{
		initialGeneration = observed;
		generation = observed;
	}
	if(observed != *initialGeneration)
		throw std::logic_error("Live graph generation changed during projected event recording");
	if(advance)
		generation = PreparedBonusGraphStorage::nextGeneration(generation);
	Event event{advance, {}};
	TCNodesVector pending;
	if(root)
		pending.push_back(root);
	while(!pending.empty())
	{
		const auto * node = pending.back();
		pending.pop_back();
		auto position = stamps.try_emplace(node, Stamp{node->getTreeVersion()}).first;
		if(position->second.version == generation)
		{
			if(!position->second.invalidated)
				borrowedCollision = true; // no preceding private event established freshness
			continue; // exact legacy early return, including no child descent
		}
		position->second = Stamp{generation, true};
		event.targets.push_back(node);
		const auto & children = graph.children(*node);
		pending.insert(pending.end(), children.rbegin(), children.rend());
	}
	if(PreparedBonusGraphStorage::currentGeneration() != *initialGeneration)
		throw std::logic_error("Live generation changed while capturing projected child closure");
	events.push_back(std::move(event));
}
