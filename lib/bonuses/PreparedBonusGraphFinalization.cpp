/*
 * PreparedBonusGraphFinalization.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "PreparedBonusGraphFinalization.h"
#include "ProjectedBonusGraph.h"
#include <map>
#include <stdexcept>

PreparedBonusGraphFinalization::PreparedBonusGraphFinalization(ProjectedBonusGraph & graph,
	const TNodesVector & mutableNodes)
{
	graph.requireUsable();
	if(graph.unjournaledReplacement)
		throw std::logic_error("Raw projected storage replacement has no legacy event journal");
	graph.synchronizeForms();
	graph.validate();
	if(graph.invalidations.borrowedCollision)
		throw std::logic_error("Borrowed generation skips an originally current dependency");
	std::map<const CBonusSystemNode *, CBonusSystemNode *> authority;
	for(auto * node : mutableNodes)
		if(!node || !authority.emplace(node, node).second)
			throw std::invalid_argument("Final graph authority contains a null or repeated node");
	if(authority.size() != graph.entries.size())
		throw std::invalid_argument("Final graph requires exact mutable authority for captured closure");
	for(const auto & [node, entry] : graph.entries)
	{
		const auto found = authority.find(node);
		if(found == authority.end())
			throw std::invalid_argument("Final graph authority does not match captured closure");
		const auto index = storage.capture(*found->second);
		storage.replace(index, entry.current);
	}
	const auto & journal = graph.invalidations;
	const auto generation = journal.initialGeneration.value_or(PreparedBonusGraphStorage::currentGeneration());
	storage.requireGeneration(generation);
	for(const auto & event : journal.events)
	{
		TNodesVector targets;
		targets.reserve(event.targets.size());
		for(const auto * node : event.targets)
			targets.push_back(authority.at(node));
		storage.addInvalidation(event.advance, std::move(targets));
	}
	storage.prepare();
	graph.requireUsable();
	graph.validate();
	if(!storage.readyToCommit())
		throw std::runtime_error("Final graph became stale during preparation");
	graph.finalized = true;
}

bool PreparedBonusGraphFinalization::readyToCommit() const noexcept
{
	return storage.readyToCommit();
}

bool PreparedBonusGraphFinalization::commit() noexcept
{
	return storage.commit();
}
