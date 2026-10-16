/*
 * ProjectedBonusGraphValidation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include <functional>
#include <stdexcept>

void ProjectedBonusGraph::validate() const
{
	for(const auto & [node, entry] : entries)
	{
		if(node->getTreeVersion() != entry.version)
			throw std::logic_error("Original graph changed outside the projection lease");
		const auto live = PreparedBonusGraphStorage::read(*node);
		if(!PreparedBonusGraphStorage::sameBonuses(live.accepted, entry.original.accepted)
			|| !PreparedBonusGraphStorage::sameBonuses(live.exported, entry.original.exported)
			|| live.parents != entry.original.parents || live.propagatingParents != entry.original.propagatingParents
			|| live.children != entry.original.children)
			throw std::logic_error("Original graph storage changed outside the projection lease");
		PreparedBonusGraphStorage::validate(entry.current);
		for(const auto * parent : entry.current.parents)
			if(!entries.contains(parent))
				throw std::logic_error("Projected inheritance endpoint was not captured");
		for(const auto * parent : entry.current.propagatingParents)
			if(!entries.contains(parent))
				throw std::logic_error("Projected propagation endpoint was not captured");
		for(const auto * child : entry.current.children)
			if(!entries.contains(child))
				throw std::logic_error("Projected child endpoint was not captured");
	}
	// Validate black and red DAGs separately. Global visited would incorrectly
	// erase legal repeated diamond paths from actual ordered bonus collection.
	for(bool red : {false, true})
	{
		std::set<const CBonusSystemNode *> active, complete;
		std::function<void(const CBonusSystemNode *)> visit = [&](const CBonusSystemNode * node)
		{
			if(complete.contains(node))
				return;
			if(!active.insert(node).second)
				throw std::logic_error("Projected graph contains a cycle");
			if(red)
			{
				TCNodes next;
				bonusRedChildren(*this, *node, next);
				for(const auto * child : next)
					visit(child);
			}
			else
				for(const auto * parent : parents(*node))
					visit(parent);
			active.erase(node);
			complete.insert(node);
		};
		for(const auto & [node, entry] : entries)
			visit(node);
	}
}
