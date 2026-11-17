/*
 * BonusGraphQuery.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BonusGraphQuery.h"
#include "BonusLimitation.h"
#include "Propagators.h"
#include "Updaters.h"
#include <type_traits>

class LiveBonusGraphQuery final : public BonusGraphQuery
{
public:
	const BonusList & local(const CBonusSystemNode & node) const override { return node.bonuses; }
	const TCNodesVector & parents(const CBonusSystemNode & node) const override { return node.parentsToInherit; }
	const TNodesVector & propagatingParents(const CBonusSystemNode & node) const override { return node.parentsToPropagate; }
	const TNodesVector & children(const CBonusSystemNode & node) const override { return node.children; }
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override { return node; }
};

namespace
{
struct NoObservation {};
template<class Observer>
void collectBonusGraphImpl(const BonusGraphQuery & graph, const CBonusSystemNode & node,
	BonusList & out, bool inherited, Observer & observer)
{
	BonusList beforeUpdate;
	for(const auto * parent : graph.parents(node))
		collectBonusGraphImpl(graph, *parent, beforeUpdate, true, observer);
	const auto & local = graph.local(node);
	local.getAllBonuses(beforeUpdate);
	if constexpr(!std::is_same_v<Observer, NoObservation>) observer.local(beforeUpdate, local, node);
	const auto context = graph.evaluation(node);
	if(inherited && node.isPrimaryRatingNode())
	{
		BonusList updated;
		for(const auto & bonus : beforeUpdate)
			updated.push_back(bonus->updater ? bonus->updater->createUpdatedBonus(bonus, context) : bonus);
		BonusList heroApplicable;
		limitBonuses(updated, heroApplicable, context);
		for(const auto & bonus : updated)
		{
			const bool rating = bonus->type == BonusType::PRIMARY_SKILL
				&& (bonus->subtype == BonusSubtypeID(PrimarySkill::ATTACK) || bonus->subtype == BonusSubtypeID(PrimarySkill::DEFENSE));
			// Keep creature-specific specialties/bonuses which do NOT apply as
			// hero ratings; filter only the hero's own effective A/D contribution.
			const bool retained = !rating || !vstd::contains(heroApplicable, bonus);
			if(retained) out.push_back(bonus);
			if constexpr(!std::is_same_v<Observer, NoObservation>) observer.updated(beforeUpdate, static_cast<size_t>(&bonus - &updated[0]), out, bonus, node, context, true, retained);
		}
		if constexpr(!std::is_same_v<Observer, NoObservation>) observer.retire(beforeUpdate);
		return;
	}
	for(const auto & bonus : beforeUpdate)
	{
		auto updated = bonus->updater ? bonus->updater->createUpdatedBonus(bonus, context) : bonus;
		out.push_back(updated);
		if constexpr(!std::is_same_v<Observer, NoObservation>) observer.updated(beforeUpdate, static_cast<size_t>(&bonus - &beforeUpdate[0]), out, updated, node, context, false, true);
	}
	if constexpr(!std::is_same_v<Observer, NoObservation>) observer.retire(beforeUpdate);
}
}

void collectBonusGraph(const BonusGraphQuery & graph, const CBonusSystemNode & node, BonusList & out, bool inherited)
{
	NoObservation observer;
	collectBonusGraphImpl(graph, node, out, inherited, observer);
}

void collectBonusGraphObserved(const BonusGraphQuery & graph, const CBonusSystemNode & node,
	BonusList & out, BonusCollectionObserver & observer, bool inherited)
{
	collectBonusGraphImpl(graph, node, out, inherited, observer);
}

void collectLiveBonusGraph(const CBonusSystemNode & node, BonusList & out, bool inherited)
{
	collectBonusGraph(liveBonusGraph(), node, out, inherited);
}

const BonusGraphQuery & liveBonusGraph()
{
	static const LiveBonusGraphQuery access;
	return access;
}

bool bonusSourceOnly(BonusNodeType type)
{
	switch(type)
	{
	case BonusNodeType::CREATURE:
	case BonusNodeType::ARTIFACT:
	case BonusNodeType::ARTIFACT_INSTANCE:
	case BonusNodeType::BOAT:
		return true;
	default:
		return false;
	}
}

void bonusRedParents(const BonusGraphQuery & graph, const CBonusSystemNode & node, TCNodes & out)
{
	TCNodes parents(graph.parents(node).begin(), graph.parents(node).end());
	for(const auto * parent : parents)
		if(bonusSourceOnly(parent->getNodeType()))
			out.insert(parent);
	if(!bonusSourceOnly(node.getNodeType()))
		for(const auto * child : graph.children(node))
			out.insert(child);
}

void bonusRedChildren(const BonusGraphQuery & graph, const CBonusSystemNode & node, TCNodes & out)
{
	for(const auto * parent : graph.propagatingParents(node))
		if(!bonusSourceOnly(parent->getNodeType()))
			out.insert(parent);
	if(bonusSourceOnly(node.getNodeType()))
		for(const auto * child : graph.children(node))
			out.insert(child);
}

void bonusRedAncestors(const BonusGraphQuery & graph, const CBonusSystemNode & node, TCNodes & out)
{
	bonusRedParents(graph, node, out);
	TCNodes parents;
	bonusRedParents(graph, node, parents);
	for(const auto * parent : parents)
		bonusRedAncestors(graph, *parent, out);
}

std::shared_ptr<Bonus> propagatedBonus(const std::shared_ptr<Bonus> & bonus,
	const CBonusSystemNode & destination, const BonusEvaluationContext & source)
{
	// Legacy predicate accepts a mutable pointer; first-party implementations
	// inspect node type only. No graph writes are performed by this helper.
	if(!bonus->propagator->shouldBeAttached(const_cast<CBonusSystemNode *>(&destination)))
		return nullptr;
	return bonus->propagationUpdater ? bonus->propagationUpdater->createUpdatedBonus(bonus, source) : bonus;
}

bool removePropagatedBonus(BonusList & local, const std::shared_ptr<Bonus> & bonus)
{
	if(bonus->propagationUpdater)
	{
		const auto before = local.size();
		local.remove_if([bonus](const auto & candidate)
		{
			return candidate->propagationUpdater && candidate->propagationUpdater == bonus->propagationUpdater;
		});
		return before != local.size();
	}
	return (local -= bonus) != 0;
}
