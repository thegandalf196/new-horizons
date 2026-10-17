/*
 * ProjectedBonusGraphRelations.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include <stdexcept>

void ProjectedBonusGraph::prepareRelation(const CBonusSystemNode & node, const CBonusSystemNode & parent)
{
	requireUsable();
	if(unjournaledReplacement)
		throw std::logic_error("Raw projected replacement is query-only, not an edge-edit program");
	synchronizeForms();
	capture(node);
	capture(parent);
	validate();
}

void ProjectedBonusGraph::redDescendantRelation(const CBonusSystemNode & source,
	const CBonusSystemNode & destination, bool attach)
{
	for(const auto & bonus : values(source).exported)
		if(bonus->propagator)
			propagate(destination, source, bonus, !attach);
	TCNodes ancestors;
	bonusRedAncestors(*this, source, ancestors);
	for(const auto * ancestor : ancestors)
		for(const auto & bonus : values(*ancestor).exported)
			if(bonus->propagator)
				propagate(destination, source, bonus, !attach);
}

void ProjectedBonusGraph::sourceRelation(const CBonusSystemNode & node, const CBonusSystemNode & source, bool attach)
{
	auto & edges = entries.at(&node).current.parents;
	if(attach)
	{
		if(vstd::contains(edges, &source))
			throw std::logic_error("Projected inheritance source is already attached");
		edges.push_back(&source);
		validate(); // reject a cycle before propagation walks
	}
	invalidations.record(true, nullptr, *this);
	if(!node.isHypothetic() && bonusSourceOnly(source.getNodeType()))
		redDescendantRelation(source, node, attach);
	if(!attach)
	{
		const auto found = std::find(edges.begin(), edges.end(), &source);
		if(found == edges.end())
			throw std::logic_error("Projected inheritance source was not attached");
		edges.erase(found);
	}
	invalidations.record(false, &node, *this);
}

void ProjectedBonusGraph::attachToSource(const CBonusSystemNode & node, const CBonusSystemNode & parent)
{
	requireUsable();
	try
	{
		prepareRelation(node, parent);
		sourceRelation(node, parent, true);
		validate();
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::detachFromSource(const CBonusSystemNode & node, const CBonusSystemNode & parent)
{
	requireUsable();
	try
	{
		prepareRelation(node, parent);
		if(!vstd::contains(parents(node), &parent))
			throw std::logic_error("Projected source detach requires an existing edge");
		sourceRelation(node, parent, false);
		validate();
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::attachTo(CBonusSystemNode & node, CBonusSystemNode & parent)
{
	requireUsable();
	try
	{
		prepareRelation(node, parent);
		auto & propagated = entries.at(&node).current.propagatingParents;
		if(vstd::contains(propagated, &parent))
			throw std::logic_error("Projected propagation parent is already attached");
		propagated.push_back(&parent);
		sourceRelation(node, parent, true);
		if(!node.isHypothetic())
		{
			if(!bonusSourceOnly(parent.getNodeType()))
				redDescendantRelation(node, parent, true);
			auto & childEdges = entries.at(&parent).current.children;
			if(vstd::contains(childEdges, &node))
				throw std::logic_error("Projected parent already owns the child");
			childEdges.push_back(&node);
		}
		validate();
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::detachFrom(CBonusSystemNode & node, CBonusSystemNode & parent)
{
	requireUsable();
	try
	{
		prepareRelation(node, parent);
		auto & propagated = entries.at(&node).current.propagatingParents;
		const auto edge = std::find(propagated.begin(), propagated.end(), &parent);
		if(edge == propagated.end() || !vstd::contains(parents(node), &parent))
			throw std::logic_error("Projected detach requires an existing full edge");
		if(!node.isHypothetic() && !bonusSourceOnly(parent.getNodeType()))
			redDescendantRelation(node, parent, false);
		sourceRelation(node, parent, false);
		propagated.erase(edge);
		if(!node.isHypothetic())
		{
			auto & childEdges = entries.at(&parent).current.children;
			const auto child = std::find(childEdges.begin(), childEdges.end(), &node);
			if(child == childEdges.end())
				throw std::logic_error("Projected parent has no reciprocal child");
			childEdges.erase(child);
		}
		invalidations.record(true, &parent, *this);
		validate();
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::detachFromAll(CBonusSystemNode & node)
{
	requireUsable();
	try
	{
		prepareRelation(node, node);
		while(!propagatingParents(node).empty())
			detachFrom(node, *propagatingParents(node).front());
		while(!parents(node).empty())
			detachFromSource(node, *parents(node).front());
	}
	catch(...) { poisoned = true; throw; }
}
