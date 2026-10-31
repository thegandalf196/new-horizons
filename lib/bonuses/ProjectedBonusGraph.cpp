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
#include "../battle/CUnitState.h"
#include "../CCreatureHandler.h"
#include <stdexcept>

void ProjectedBonusGraph::requireUsable() const
{
	if(poisoned || finalized)
		throw std::logic_error("Projected graph preparation failed; discard its owner");
	for(const auto & [original, binding] : bindings)
		if(binding.occurrence && !hasExactView(*original, *binding.unit))
			throw std::logic_error("Projected occurrence descriptor/provider was replaced");
}

void ProjectedBonusGraph::requireBoundUnit(const battle::Unit & unit) const
{
	requireUsable();
	for(const auto & [original, binding] : bindings)
		if(binding.unit == &unit && hasExactView(*original, *binding.unit))
			return;
	throw std::invalid_argument("Projected environment requires this graph's exact bound provider");
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
			auto current = value;
			if(!durationAliases.empty()) // Ordinary capture retains its old copy-only path.
				for(auto * list : {&current.accepted, &current.exported})
					for(size_t i = 0; i < list->size(); ++i)
						(*list)[i] = currentDurationRecord((*list)[i]);
			entries.emplace(next, Entry{value, std::move(current), next->getTreeVersion()});
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

const BonusList & ProjectedBonusGraph::local(const CBonusSystemNode & node) const { return values(node).accepted; }
const TCNodesVector & ProjectedBonusGraph::parents(const CBonusSystemNode & node) const { return values(node).parents; }
const TNodesVector & ProjectedBonusGraph::propagatingParents(const CBonusSystemNode & node) const { return values(node).propagatingParents; }
const TNodesVector & ProjectedBonusGraph::children(const CBonusSystemNode & node) const { return values(node).children; }

BonusEvaluationContext ProjectedBonusGraph::evaluation(const CBonusSystemNode & node) const
{
	if(const auto * stack = dynamic_cast<const CStack *>(&node))
		if(const auto found = bindings.find(stack); found != bindings.end())
		{
			if(found->second.nativeBattle)
			{
				const CGHeroInstance * hero = nullptr;
				for(const auto * parent : parents(*stack))
					if(parent->getNodeType() == BonusNodeType::HERO)
					{
						hero = dynamic_cast<const CGHeroInstance *>(parent);
						break; // exact native first HERO parent, including failed typed cast
					}
				return BonusEvaluationContext(*stack, *found->second.unit, *found->second.roster,
					{hero, *found->second.nativeBattle, stack->getQueryTerrain()});
			}
			return BonusEvaluationContext(*stack, *found->second.unit, *found->second.roster);
		}
	return node;
}

TConstBonusListPtr ProjectedBonusGraph::queryOriginal(const CBonusSystemNode & node, const CSelector & selector, bool unstacked)
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

TConstBonusListPtr ProjectedBonusGraph::query(const CBonusSystemNode & node, const CSelector & selector, bool unstacked)
{
	requireUsable();
	if(const auto * stack = dynamic_cast<const CStack *>(&node))
		if(const auto found = recipientOverlays.find(stack); found != recipientOverlays.end())
			return queryRecipientLayer(*stack, found->second.size(), selector, unstacked);
	return queryOriginal(node, selector, unstacked);
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

std::vector<Bonus> ProjectedBonusGraph::View::sourceLocalSpellEffects() const
{
	graph.requireUsable();
	graph.synchronizeForms();
	graph.validate();
	std::vector<Bonus> result;
	for(const auto & bonus : graph.values(node).exported)
		if(bonus->source == BonusSource::SPELL_EFFECT)
			result.push_back(*bonus);
	return result;
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

void ProjectedBonusGraph::requireBoundUnit(const CStack & original, const battle::CUnitState & unit) const
{
	requireUsable();
	const auto found = bindings.find(&original);
	if(found == bindings.end() || found->second.unit != &unit || !hasExactView(original, unit))
		throw std::invalid_argument("Projected operation requires this original and exact bound state");
}

const std::vector<ProjectedBonusInvalidations::Event> & ProjectedBonusGraph::invalidationEvents() const
{
	requireUsable();
	return invalidations.recordedEvents();
}
