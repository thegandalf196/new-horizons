/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedRecipientBonusGraph.h"
#include "BonusLimitation.h"
#include <stdexcept>
namespace
{
class RootQueryGraph final : public BonusGraphQuery
{
	const OwnedRecipientBonusGraph & graph;
	const OwnedBonusEvaluationFacts & facts;
	const TCNodesVector & rootParents;
	const BonusList & rootRecords;
	const battle::Unit * ownedUnit;
public:
	RootQueryGraph(const OwnedRecipientBonusGraph & graph, const OwnedBonusEvaluationFacts & facts,
		const TCNodesVector & parents, const BonusList & records, const battle::Unit * unit)
		: graph(graph), facts(facts), rootParents(parents), rootRecords(records), ownedUnit(unit) {}
	const BonusList & local(const CBonusSystemNode & node) const override
	{
		return &node == &graph.root() ? rootRecords : graph.local(node);
	}
	const TCNodesVector & parents(const CBonusSystemNode & node) const override
	{
		return &node == &graph.root() ? rootParents : graph.parents(node);
	}
	const TNodesVector & propagatingParents(const CBonusSystemNode & node) const override { return graph.propagatingParents(node); }
	const TNodesVector & children(const CBonusSystemNode & node) const override { return graph.children(node); }
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override
	{
		if(&node != &graph.root()) return graph.evaluation(node);
		return ownedUnit ? BonusEvaluationContext(node, facts, *ownedUnit) : BonusEvaluationContext(node, facts);
	}
};
}
// Node-private topology/payload inspection is supplied by the owning storage TU.
BonusList OwnedRecipientBonusGraph::queryRoot(const CSelector & selector, bool unstacked, const RootQuery & query) const
{
	BonusList selectedRaw;
	for(const auto index : query.records) selectedRaw.push_back(payload(index));
	return queryRootRecords(selector, unstacked, query, selectedRaw);
}
BonusList OwnedRecipientBonusGraph::queryRootRecords(const CSelector & selector, bool unstacked,
	const RootQuery & query, const BonusList & selectedRaw, const battle::Unit * ownedUnit) const
{
	validateRootQuery(query);
	TCNodesVector selectedParents;
	for(const auto index : query.parents) selectedParents.push_back(&queryNode(index));
	RootQueryGraph view(*this, query.facts, selectedParents, selectedRaw, ownedUnit);
	BonusList all, accepted, selected;
	collectBonusGraph(view, root(), all);
	limitBonuses(all, accepted, view.evaluation(root()));
	if(!unstacked) accepted.stackBonuses();
	accepted.getBonuses(selected, selector);
	return selected;
}
