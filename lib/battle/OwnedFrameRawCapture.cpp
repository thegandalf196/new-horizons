/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameRawCapture.h"
#include "../CStack.h"
#include <map>
namespace battle::ownedFrameCapture
{
RawGraph rawGraph(const BonusGraphQuery & graph, const CBonusSystemNode & root)
{
	RawGraph result; result.nodes = {&root};
	std::map<const CBonusSystemNode *, size_t> indices{{&root, 0}};
	for(size_t i = 0; i < result.nodes.size(); ++i)
	{
		const auto * source = result.nodes[i];
		OwnedRecipientBonusGraph::NodeInput input; input.facts.nodeType = source->getNodeType();
		for(const auto * parent : graph.parents(*source))
		{
			auto [found, inserted] = indices.emplace(parent, result.nodes.size());
			if(inserted) result.nodes.push_back(parent);
			input.parents.push_back(found->second); // Preserve order and every repeated parent visit.
		}
		const auto & local = graph.local(*source); input.local.assign(local.begin(), local.end());
		input.primaryRating = source->isPrimaryRatingNode(); result.input.nodes.push_back(std::move(input));
	}
	return result;
}
void finishFacts(RawGraph & graph, const CStack * recipient, const CCreature * effective,
	const std::vector<const CCreature *> & visibleForms)
{
	std::vector<BonusList> raw(graph.input.nodes.size() + 1);
	for(size_t i = 0; i < graph.input.nodes.size(); ++i)
		for(const auto & record : graph.input.nodes[i].local) raw[i].push_back(std::const_pointer_cast<Bonus>(record));
	for(const auto & record : graph.input.sourceExports) raw.back().push_back(std::const_pointer_cast<Bonus>(record));
	for(const auto & record : graph.input.retainedRecords) raw.back().push_back(std::const_pointer_cast<Bonus>(record));
	const auto needed = requirements(raw); // Pure finite payload admission, BEFORE any evaluator runs.
	for(size_t i = 0; i < graph.nodes.size(); ++i)
	{
		graph.input.nodes[i].facts = facts(*graph.nodes[i], needed, visibleForms, recipient, effective);
		if(!recipient) // Creature trait roots are not battle occurrences: exact empty adjacency domain.
		{
			graph.input.nodes[i].facts.adjacencyUnits.emplace();
			graph.input.nodes[i].facts.adjacencyTraits.emplace();
		}
	}
}
}
