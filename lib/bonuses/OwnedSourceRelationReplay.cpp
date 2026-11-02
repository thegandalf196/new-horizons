/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedSourceRelationReplay.h"
#include "BonusSourceRelationRecipe.h"
#include "PeerBonusDependencyClosure.h"
#include <stdexcept>

void validateOwnedSourceRelations(const OwnedRecipientBonusGraph::CaptureInput & input,
	const std::vector<std::shared_ptr<const Bonus>> & raw,
	const std::vector<OwnedRecipientBonusGraph::SourceRelation> & relations)
{
	size_t localCount = 0;
	for(const auto & node : input.nodes) localCount += node.local.size();
	for(const auto & relation : relations)
	{
		relation.source.validate();
		if(relation.source.nodeType != BonusNodeType::CREATURE)
			throw std::invalid_argument("Owned form relation requires its immediate creature source");
		for(const auto index : relation.records)
			if(index < localCount || index >= raw.size() || !raw[index] || !raw[index]->propagator)
				throw std::invalid_argument("Invalid owned source export handle");
		for(const auto index : relation.destinations)
			if(index >= input.nodes.size() || bonusSourceOnly(input.nodes[index].facts.nodeType))
				throw std::invalid_argument("Owned form propagation requires parent-only nonsource destinations");
	}
}

std::vector<std::shared_ptr<Bonus>> replayOwnedSourceRelations(
	OwnedRecipientBonusGraph::CaptureInput & input, std::vector<std::shared_ptr<Bonus>> records,
	const std::vector<OwnedRecipientBonusGraph::SourceRelation> & relations)
{
	if(relations.empty() && !input.peerDependenciesOnly) return records;
	std::vector<BonusList> local(input.nodes.size());
	std::vector<std::unique_ptr<CBonusSystemNode>> destinations;
	size_t offset = 0;
	for(size_t i = 0; i < input.nodes.size(); ++i)
	{
		destinations.push_back(std::make_unique<CBonusSystemNode>(input.nodes[i].facts.nodeType, true));
		for(size_t j = 0; j < input.nodes[i].local.size(); ++j) local[i].push_back(records.at(offset++));
	}
	const auto extraStart = offset;
	for(const auto & relation : relations)
	{
		CBonusSystemNode source(relation.source.nodeType, true); // No native attach/invalidation/globalCounter change.
		for(const auto record : relation.records)
			for(const auto destination : relation.destinations)
			{
				if(relation.attach)
					appendPropagatedArrival(local[destination], records.at(record), *destinations[destination],
						BonusEvaluationContext(source, relation.source));
				else
					removePropagatedArrival(local[destination], records.at(record), *destinations[destination]);
			}
	}
	if(input.peerDependenciesOnly) local = ownedRecipientPeer::dependencyClosure(local);
	std::vector<std::shared_ptr<Bonus>> result;
	for(size_t i = 0; i < input.nodes.size(); ++i)
	{
		input.nodes[i].local.assign(local[i].begin(), local[i].end());
		result.insert(result.end(), local[i].begin(), local[i].end());
	}
	result.insert(result.end(), records.begin() + extraStart, records.end());
	return result;
}
