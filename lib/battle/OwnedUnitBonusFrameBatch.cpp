/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitBonusFrameBatch.h"
#include <stdexcept>
#include <unordered_set>
namespace battle
{
std::vector<std::shared_ptr<OwnedUnitBonusFrame>> OwnedUnitBonusFrameBatch::fromOwnedInputs(
	std::vector<OwnedRecipientBonusGraph::CaptureInput> inputs, std::vector<Recipe> recipes,
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
	auto captured = OwnedRecipientBonusGraph::fromOwnedBatchInputs(std::move(inputs), codec);
	std::vector<std::shared_ptr<const OwnedRecipientBonusGraph>> graphs;
	for(auto & value : captured) graphs.emplace_back(std::move(value));
	std::vector<std::shared_ptr<OwnedUnitBonusFrame>> result;
	for(auto & recipe : recipes)
	{
		for(auto & [form, source] : recipe.inputs.forms)
		{
			const auto indices = recipe.formGraphs.at(form);
			source.fullGraph = graphs.at(indices.first); source.nativeGraph = graphs.at(indices.second);
		}
		recipe.inputs.peerInputs.emplace();
		for(auto & peer : recipe.peers)
		{
			OwnedUnitBonusFrame::PeerInput value{peer.identity, graphs.at(peer.graph), std::move(peer.query), peer.values, {}};
			for(auto & [form, source] : peer.variants)
				value.sourceVariants.emplace(form, OwnedUnitBonusFrame::QuerySource{graphs.at(source.first), std::move(source.second)});
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
