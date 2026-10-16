/*
 * ProjectedBonusGraph.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include "ProjectedBonusRoster.h"
#include "BonusLimitation.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include <stdexcept>

void ProjectedBonusGraph::requireUsable() const
{
	if(poisoned || finalized)
		throw std::logic_error("Projected graph preparation failed; discard its owner");
}

void ProjectedBonusGraph::capture(const CBonusSystemNode & node)
{
	requireUsable();
	std::vector<const CBonusSystemNode *> pending{&node};
	try
	{
		while(!pending.empty())
		{
			const auto * next = pending.back();
			pending.pop_back();
			if(entries.contains(next))
				continue;
			auto value = PreparedBonusGraphStorage::read(*next);
			PreparedBonusGraphStorage::validate(value);
			entries.emplace(next, Entry{value, value, next->getTreeVersion()});
			pending.insert(pending.end(), value.parents.begin(), value.parents.end());
			pending.insert(pending.end(), value.propagatingParents.begin(), value.propagatingParents.end());
			pending.insert(pending.end(), value.children.begin(), value.children.end());
		}
	}
	catch(...) { poisoned = true; throw; }
}

TCNodesVector ProjectedBonusGraph::capturedNodes() const
{
	requireUsable();
	TCNodesVector result;
	result.reserve(entries.size());
	for(const auto & entry : entries)
		result.push_back(entry.first);
	return result;
}

const ProjectedBonusGraph::Values & ProjectedBonusGraph::values(const CBonusSystemNode & node) const
{
	requireUsable();
	return entries.at(&node).current;
}

void ProjectedBonusGraph::replace(const CBonusSystemNode & node, Values value)
{
	requireUsable();
	PreparedBonusGraphStorage::validate(value);
	try
	{
		entries.at(&node).current = std::move(value);
		unjournaledReplacement = true;
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::bind(const CStack & original, const battle::CUnitState & unit,
	const ProjectedBonusRoster & roster)
{
	requireUsable();
	BonusEvaluationContext check(original, unit, roster);
	(void)check;
	if(!hasExactView(original, unit))
		throw std::invalid_argument("Projected binding requires this graph's exact original view");
	if(bindings.contains(&original))
		throw std::logic_error("Projected original already has a bound unit");
	for(const auto & [source, binding] : bindings)
		if(binding.unit == &unit || source->unitId() == original.unitId())
			throw std::invalid_argument("Projected unit identity is already bound");
	capture(original);
	bindings.emplace(&original, Binding{&unit, &roster, original.formBonusSource});
}

const BonusList & ProjectedBonusGraph::local(const CBonusSystemNode & node) const { return values(node).accepted; }
const TCNodesVector & ProjectedBonusGraph::parents(const CBonusSystemNode & node) const { return values(node).parents; }
const TNodesVector & ProjectedBonusGraph::propagatingParents(const CBonusSystemNode & node) const { return values(node).propagatingParents; }
const TNodesVector & ProjectedBonusGraph::children(const CBonusSystemNode & node) const { return values(node).children; }

BonusEvaluationContext ProjectedBonusGraph::evaluation(const CBonusSystemNode & node) const
{
	if(const auto * stack = dynamic_cast<const CStack *>(&node))
		if(const auto found = bindings.find(stack); found != bindings.end())
			return BonusEvaluationContext(*stack, *found->second.unit, *found->second.roster);
	return node;
}

TConstBonusListPtr ProjectedBonusGraph::query(const CBonusSystemNode & node, const CSelector & selector, bool unstacked)
{
	requireUsable();
	synchronizeForms();
	validate();
	BonusList collected, accepted;
	collectBonusGraph(*this, node, collected);
	limitBonuses(collected, accepted, evaluation(node));
	auto result = std::make_shared<BonusList>();
	if(!unstacked)
		accepted.stackBonuses(); // live cached path stacks before selection
	accepted.getBonuses(*result, selector);
	return result;
}

const ProjectedBonusView & ProjectedBonusGraph::view(const CBonusSystemNode & node)
{
	requireUsable();
	capture(node);
	auto & result = views[&node];
	if(!result)
		result = std::make_unique<View>(*this, node);
	return *result;
}

const battle::Unit * ProjectedBonusGraph::View::evaluatedUnit() const
{
	const auto * stack = dynamic_cast<const CStack *>(&node);
	const auto found = graph.bindings.find(stack);
	return found == graph.bindings.end() ? nullptr : found->second.unit;
}

TConstBonusListPtr ProjectedBonusGraph::View::getAllBonuses(const CSelector & selector, const std::string &) const
{
	return graph.query(node, selector, false);
}
TConstBonusListPtr ProjectedBonusGraph::View::getUnstackedBonuses(const CSelector & selector) const
{
	return graph.query(node, selector, true);
}
int32_t ProjectedBonusGraph::View::getTreeVersion() const { return node.getTreeVersion(); }
