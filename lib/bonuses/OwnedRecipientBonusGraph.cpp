/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedRecipientBonusGraph.h"
#include "BonusLimitation.h"
#include "Limiters.h"
#include <stdexcept>
#include <typeinfo>
#include <unordered_set>
namespace
{
void validateTopology(const std::vector<OwnedRecipientBonusGraph::NodeInput> & inputs, size_t root)
{
	if(root >= inputs.size()) throw std::invalid_argument("Missing owned bonus root");
	std::vector<uint8_t> color(inputs.size());
	std::vector<std::pair<size_t, size_t>> path{{root, 0}};
	color[root] = 1;
	while(!path.empty())
	{
		auto & top = path.back();
		if(top.second == inputs[top.first].parents.size())
		{
			color[top.first] = 2;
			path.pop_back();
			continue;
		}
		const auto parent = inputs[top.first].parents[top.second++];
		if(parent >= inputs.size()) throw std::invalid_argument("Invalid owned bonus parent");
		if(color[parent] == 1) throw std::invalid_argument("Cyclic owned bonus inheritance");
		if(!color[parent]) { color[parent] = 1; path.emplace_back(parent, 0); }
	}
	for(const auto state : color)
		if(!state) throw std::invalid_argument("Unrelated owned bonus node outside recipient closure");
}
void requireCapturedLimiterDomain(const TLimiterPtr & root)
{
	std::vector<TLimiterPtr> pending{root};
	std::unordered_set<const ILimiter *> seen;
	while(!pending.empty())
	{
		const auto value = pending.back();
		pending.pop_back();
		if(!value || !seen.insert(value.get()).second) continue;
		const auto & type = typeid(*value);
		if(type == typeid(AllOfLimiter) || type == typeid(AnyOfLimiter) || type == typeid(NoneOfLimiter))
		{
			const auto & children = static_cast<const AggregateLimiter &>(*value).limiters;
			pending.insert(pending.end(), children.begin(), children.end());
		}
		else if(type == typeid(CCreatureTypeLimiter) || type == typeid(CreatureLevelLimiter)
			|| type == typeid(CreatureAlignmentLimiter) || type == typeid(FactionLimiter)
			|| type == typeid(RankRangeLimiter) || type == typeid(TerrainLimiter))
		{
			// Capability is checked at the canonical evaluation destination, not the
			// raw source node. Inherited records can be limited again at the root.
		}
		else if(type != typeid(ILimiter) && type != typeid(HasAnotherBonusLimiter)
			&& type != typeid(OppositeSideLimiter) && type != typeid(HasChargesLimiter))
			throw std::invalid_argument("Owned current position/adjacency frame is not bound");
	}
}
}
struct OwnedRecipientBonusGraph::Node final : CBonusSystemNode
{
	OwnedBonusEvaluationFacts facts;
	bool primary;
	BonusList values;
	TCNodesVector inheritance;
	explicit Node(const NodeInput & input)
		: CBonusSystemNode(input.facts.nodeType, true), facts(input.facts), primary(input.primaryRating) {}
	bool isPrimaryRatingNode() const override { return primary; }
};
OwnedRecipientBonusGraph::~OwnedRecipientBonusGraph() = default;
std::unique_ptr<OwnedRecipientBonusGraph> OwnedRecipientBonusGraph::fromOwnedInputs(
	std::vector<NodeInput> inputs, size_t root, std::vector<std::shared_ptr<const Bonus>> extraRecords)
{
	validateTopology(inputs, root);
	std::vector<std::shared_ptr<const Bonus>> raw;
	for(const auto & input : inputs)
	{
		input.facts.validate();
		for(const auto & value : input.local)
		{
			if(!value) throw std::invalid_argument("Null owned bonus local record");
			requireCapturedLimiterDomain(value->limiter);
			raw.push_back(value);
		}
	}
	const auto localCount = raw.size();
	for(const auto & value : extraRecords)
	{
		if(!value) throw std::invalid_argument("Null owned extra bonus record");
		requireCapturedLimiterDomain(value->limiter);
		raw.push_back(value);
	}
	auto result = std::unique_ptr<OwnedRecipientBonusGraph>(new OwnedRecipientBonusGraph);
	result->payloads = ownedBonusPayloads::copy(raw);
	result->localPayloadCount = localCount;
	result->rootIndex = root;
	for(const auto & input : inputs) result->nodes.push_back(std::make_unique<Node>(input));
	size_t offset = 0;
	for(size_t i = 0; i < inputs.size(); ++i)
	{
		auto & node = *result->nodes[i];
		if(i == root) result->rootPayloadStart = offset;
		result->indices.emplace(&node, i);
		for(size_t j = 0; j < inputs[i].local.size(); ++j) node.values.push_back(result->payloads->roots().at(offset++));
		for(const auto parent : inputs[i].parents) node.inheritance.push_back(result->nodes.at(parent).get());
	}
	for(size_t i = 0; i < result->nodes.size(); ++i)
		if(i != root) for(const auto & record : result->nodes[i]->values) result->inheritedOnlyRecords.insert(record.get());
	for(const auto & record : result->nodes[root]->values) result->inheritedOnlyRecords.erase(record.get());
	return result;
}
const OwnedRecipientBonusGraph::Node & OwnedRecipientBonusGraph::entry(const CBonusSystemNode & node) const
{
	const auto found = indices.find(&node);
	if(found == indices.end()) throw std::invalid_argument("Foreign owned bonus node");
	return *nodes.at(found->second);
}
const CBonusSystemNode & OwnedRecipientBonusGraph::root() const { return *nodes.at(rootIndex); }
const BonusList & OwnedRecipientBonusGraph::local(const CBonusSystemNode & node) const { return entry(node).values; }
const TCNodesVector & OwnedRecipientBonusGraph::parents(const CBonusSystemNode & node) const { return entry(node).inheritance; }
const TNodesVector & OwnedRecipientBonusGraph::propagatingParents(const CBonusSystemNode & node) const
{
	entry(node);
	return emptyEdges;
}
const TNodesVector & OwnedRecipientBonusGraph::children(const CBonusSystemNode & node) const
{
	entry(node);
	return emptyEdges;
}
BonusEvaluationContext OwnedRecipientBonusGraph::evaluation(const CBonusSystemNode & node) const
{
	return {node, entry(node).facts};
}
BonusList OwnedRecipientBonusGraph::collect() const
{
	BonusList result;
	collectBonusGraph(*this, root(), result);
	return result;
}
BonusList OwnedRecipientBonusGraph::query(const CSelector & selector, bool unstacked) const
{
	const auto all = collect();
	BonusList accepted;
	limitBonuses(all, accepted, evaluation(root()));
	if(!unstacked) accepted.stackBonuses();
	BonusList selected;
	accepted.getBonuses(selected, selector);
	return selected;
}
std::vector<size_t> OwnedRecipientBonusGraph::rootLocalRecords() const
{
	std::vector<size_t> result;
	for(size_t i = 0; i < local(root()).size(); ++i) result.push_back(rootPayloadStart + i);
	return result;
}
size_t OwnedRecipientBonusGraph::payloadCount() const { return payloads->roots().size(); }
const std::shared_ptr<Bonus> & OwnedRecipientBonusGraph::payload(size_t index) const { return payloads->roots().at(index); }
const CBonusSystemNode & OwnedRecipientBonusGraph::queryNode(size_t index) const { return *nodes.at(index); }
void OwnedRecipientBonusGraph::validateRootQuery(const RootQuery & query) const
{
	query.facts.validate();
	if(query.facts.nodeType != root().getNodeType()) throw std::invalid_argument("Owned root query type mismatch");
	const auto & captured = parents(root());
	auto next = captured.begin();
	for(const auto index : query.parents)
	{
		next = std::find(next, captured.end(), &queryNode(index));
		if(next == captured.end()) throw std::invalid_argument("Owned query parents must preserve captured order and multiplicity");
		++next;
	}
	for(const auto index : query.records)
	{
		payload(index); // Bounds first; no foreign or dangling raw identities.
		if((index < localPayloadCount && (index < rootPayloadStart || index >= rootPayloadStart + local(root()).size()))
			|| inheritedOnlyRecords.contains(payload(index).get()))
			throw std::invalid_argument("Inherited source record cannot become recipient-local raw input");
	}
}
