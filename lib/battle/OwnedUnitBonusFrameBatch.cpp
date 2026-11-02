/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitBonusFrameBatch.h"
#include <stdexcept>
#include <unordered_set>
namespace battle
{
namespace
{
OwnedRecipientBonusGraph::RootQuery derivedQuery(
	const std::vector<std::shared_ptr<const OwnedRecipientBonusGraph>> & graphs,
	const std::map<CreatureID, size_t> & traitsByCreature, size_t graph)
{
	auto result = graphs.at(graph)->capturedRootQuery();
	if(!traitsByCreature.empty() && result.facts.adjacencyTraits)
	{
		for(auto & [creature, facts] : *result.facts.adjacencyTraits)
		{
			if(!facts) continue;
			const auto & traits = *graphs.at(traitsByCreature.at(creature));
			facts->undead = !traits.query(Selector::type()(BonusType::UNDEAD), true).empty();
			facts->living = !traits.query(Selector::type()(BonusType::LIVING), true).empty();
		}
	}
	return result;
}
}
std::vector<std::shared_ptr<OwnedUnitBonusFrame>> OwnedUnitBonusFrameBatch::fromOwnedInputs(
	std::vector<OwnedRecipientBonusGraph::CaptureInput> inputs, std::vector<Recipe> recipes,
	const OwnedBonusCaptureCodec & codec)
{
	return build(std::move(inputs), std::move(recipes), std::nullopt, codec);
}
std::vector<std::shared_ptr<OwnedUnitBonusFrame>> OwnedUnitBonusFrameBatch::fromOwnedSourceRecipes(
	std::vector<OwnedRecipientBonusGraph::CaptureInput> inputs, std::vector<Recipe> recipes,
	std::vector<std::vector<OwnedRecipientBonusGraph::SourceRelation>> relations,
	const OwnedBonusCaptureCodec & codec)
{
	return build(std::move(inputs), std::move(recipes), std::move(relations), codec);
}
std::vector<std::shared_ptr<OwnedUnitBonusFrame>> OwnedUnitBonusFrameBatch::build(
	std::vector<OwnedRecipientBonusGraph::CaptureInput> inputs, std::vector<Recipe> recipes,
	std::optional<std::vector<std::vector<OwnedRecipientBonusGraph::SourceRelation>>> relations,
	const OwnedBonusCaptureCodec & codec)
{
	if(recipes.empty()) throw std::invalid_argument("Missing owned frame batch recipes");
	std::unordered_set<size_t> baseRoots;
	for(const auto & recipe : recipes)
	{
		if(recipe.baseGraph >= inputs.size() || !baseRoots.insert(recipe.baseGraph).second
			|| !recipe.inputs.nodes.empty() || !recipe.inputs.extraRecords.empty() || recipe.inputs.captureCodec
			|| !recipe.inputs.environmentUnits.empty() || recipe.inputs.peerInputs
			|| recipe.formGraphs.size() != recipe.inputs.forms.size())
			throw std::invalid_argument("Batch construction requires unique raw base roots and no imported frame inputs");
	}
	std::vector<size_t> localCounts(inputs.size()), extraCounts(inputs.size());
	for(size_t i = 0; i < inputs.size(); ++i)
	{
		for(const auto & node : inputs[i].nodes) localCounts[i] += node.local.size();
		extraCounts[i] = inputs[i].extraRecords.size();
	}
	auto captured = relations
		? OwnedRecipientBonusGraph::fromOwnedBatchInputs(std::move(inputs), std::move(*relations), codec)
		: OwnedRecipientBonusGraph::fromOwnedBatchInputs(std::move(inputs), codec);
	std::vector<std::shared_ptr<const OwnedRecipientBonusGraph>> graphs;
	for(auto & value : captured) graphs.emplace_back(std::move(value));
	std::vector<std::shared_ptr<OwnedUnitBonusFrame>> result;
	for(auto & recipe : recipes)
	{
		if(relations)
			for(auto & record : recipe.inputs.sourceLocalSpells)
			{
				if(record < localCounts.at(recipe.baseGraph))
					throw std::invalid_argument("Derived source exports require explicit extra-record handles");
				const auto ordinal = record - localCounts.at(recipe.baseGraph);
				record = ordinal < extraCounts.at(recipe.baseGraph)
					? graphs.at(recipe.baseGraph)->extraRecord(ordinal)
					: graphs.at(recipe.baseGraph)->sourceExportRecord(ordinal - extraCounts.at(recipe.baseGraph));
			}

		for(auto & [form, source] : recipe.inputs.forms)
		{
			const auto indices = recipe.formGraphs.at(form);
			source.fullGraph = graphs.at(indices.first); source.nativeGraph = graphs.at(indices.second);
			if(relations)
			{
				source.full = derivedQuery(graphs, recipe.traitGraphs, indices.first);
				source.native = derivedQuery(graphs, recipe.traitGraphs, indices.second);
			}
		}
		recipe.inputs.peerInputs.emplace();
		for(auto & peer : recipe.peers)
		{
			OwnedUnitBonusFrame::PeerInput value{peer.identity, graphs.at(peer.graph), std::move(peer.query), peer.values, {}};
			if(relations) value.query = derivedQuery(graphs, recipe.traitGraphs, peer.graph);
			for(auto & [form, source] : peer.variants)
				value.sourceVariants.emplace(form, OwnedUnitBonusFrame::QuerySource{graphs.at(source.first),
					relations ? derivedQuery(graphs, recipe.traitGraphs, source.first) : std::move(source.second)});
			recipe.inputs.peerInputs->push_back(std::move(value));
		}
		auto frame = OwnedUnitBonusFrame::build(std::move(recipe.inputs), recipe.values, graphs.at(recipe.baseGraph));
		if(recipe.formGraphs.at(frame->unit().battleFormCreature()).first != recipe.baseGraph)
			throw std::invalid_argument("Base handle domain must be the initial current FULL recipe");
		result.push_back(std::move(frame));
	}
	return result; // Root graph construction is unique to this batch; only fork may share a BASE control identity.
}
}
