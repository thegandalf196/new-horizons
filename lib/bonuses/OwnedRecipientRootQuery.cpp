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
public:
	RootQueryGraph(const OwnedRecipientBonusGraph & graph, const OwnedBonusEvaluationFacts & facts,
		const TCNodesVector & parents, const BonusList & records)
		: graph(graph), facts(facts), rootParents(parents), rootRecords(records) {}
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
		return &node == &graph.root() ? BonusEvaluationContext(node, facts) : graph.evaluation(node);
	}
};
}
// Node-private topology/payload inspection is supplied by the owning storage TU.
BonusList OwnedRecipientBonusGraph::queryRoot(const CSelector & selector, bool unstacked, const RootQuery & query) const
{
	validateRootQuery(query);
	TCNodesVector selectedParents;
	for(const auto index : query.parents) selectedParents.push_back(&queryNode(index));
	BonusList selectedRaw;
	for(const auto index : query.records) selectedRaw.push_back(payload(index));
	RootQueryGraph view(*this, query.facts, selectedParents, selectedRaw);
	BonusList all, accepted, selected;
	collectBonusGraph(view, root(), all);
	limitBonuses(all, accepted, view.evaluation(root()));
	if(!unstacked) accepted.stackBonuses();
	accepted.getBonuses(selected, selector);
	return selected;
}
