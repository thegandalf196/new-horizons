/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameBatchBuilder.h"
#include "OwnedFrameTopology.h"
#include "OwnedFrameRelationCapture.h"
#include "../CStack.h"
#include <set>
namespace battle::ownedFrameCapture
{
OwnedUnitBonusFrame::Identity identity(const CStack & source)
{
	return {source.unitBaseAmount(), source.unitId(), source.unitSide(), source.unitOwner(), source.unitSlot(),
		source.battleFormOriginalCreature(), {source.getBattleStartMaximumAggregateHP(), source.getRebirthOriginalAggregateHP()}};
}
Builder::Builder(const std::vector<const CStack *> & visible)
{
	std::set<CreatureID> found;
	std::vector<OwnedUnitCreatureResolver::Creature> descriptors;
	for(const auto * unit : visible)
		for(const auto form : {unit->battleFormCreature(), unit->battleFormOriginalCreature()})
			if(found.insert(form).second)
			{
				const auto * type = form.toCreature(); visibleForms.push_back(type); descriptors.push_back(descriptor(*type));
			}
	creatures = std::make_shared<OwnedUnitCreatureResolver>(std::move(descriptors));
	for(const auto * type : visibleForms)
	{
		auto raw = rawGraph(liveBonusGraph(), *type); finishFacts(raw, nullptr, nullptr, visibleForms);
		traitGraphs.emplace(type->getId(), graphs.size()); graphs.push_back(std::move(raw.input)); relations.emplace_back();
	}
}
size_t Builder::variant(const CStack & main, const CStack & query, CreatureID form, bool native, bool peer)
{
	Topology topology(main, form, native);
	auto raw = rawGraph(topology, query);
	if(!peer && !native)
		for(const auto & record : query.getExportedBonusList()) raw.input.sourceExports.push_back(record);
	std::vector<OwnedRecipientBonusGraph::SourceRelation> edits;
	if(!native)
	{
		const auto actual = stackFormSourceIdentity(main);
		const auto * desired = form != actual.baseCreature ? form.toCreature() : nullptr;
		if(desired != actual.attached)
		{
			const Requirements none;
			if(actual.attached) edits.push_back(captureFormRelation(main, *actual.attached, false,
				facts(*actual.attached, none, visibleForms), raw.nodes, raw.input));
			if(desired) edits.push_back(captureFormRelation(main, *desired, true,
				facts(*desired, none, visibleForms), raw.nodes, raw.input));
		}
	}
	finishFacts(raw, &query, (peer ? query.battleFormCreature() : form).toCreature(), visibleForms);
	raw.input.peerDependenciesOnly = peer;
	const auto result = graphs.size(); graphs.push_back(std::move(raw.input)); relations.push_back(std::move(edits));
	return result;
}
void Builder::recipient(const CStack & source, const std::vector<const CStack *> & visible,
	OwnedUnitEnvironment::Facts environment, size_t position)
{
	OwnedUnitBonusFrameBatch::Recipe recipe;
	recipe.inputs.identity = identity(source); recipe.inputs.creatures = creatures; recipe.traitGraphs = traitGraphs;
	recipe.inputs.environment = std::move(environment); recipe.inputs.recipientPosition = position; recipe.values = &source;
	for(const auto * peer : visible)
		if(peer != &source) recipe.peers.push_back({identity(*peer), 0, {}, peer, {}});
	for(const auto form : std::set<CreatureID>{source.battleFormCreature(), source.battleFormOriginalCreature()})
	{
		const auto full = variant(source, source, form, false, false);
		const auto native = variant(source, source, form, true, false);
		recipe.formGraphs.emplace(form, std::pair{full, native}); recipe.inputs.forms.emplace(form, OwnedUnitBonusFrame::FormSources{});
		if(form == source.battleFormCreature())
		{
			recipe.baseGraph = full; auto & raw = graphs.at(full);
			size_t offset = raw.extraRecords.size();
			for(const auto & node : raw.nodes) offset += node.local.size();
			for(const auto & record : raw.sourceExports)
			{
				if(record->source == BonusSource::SPELL_EFFECT) recipe.inputs.sourceLocalSpells.push_back(offset);
				++offset;
			}
		}
		size_t index = 0;
		for(const auto * peer : visible)
		{
			if(peer == &source) continue;
			const auto graph = variant(source, *peer, form, false, true);
			auto & value = recipe.peers.at(index++);
			if(form == source.battleFormCreature()) value.graph = graph;
			value.variants.emplace(form, std::pair{graph, OwnedRecipientBonusGraph::RootQuery{}});
		}
	}
	recipes.push_back(std::move(recipe));
}
}
