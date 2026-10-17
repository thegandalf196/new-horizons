/*
 * ProjectedBonusRecipientOverlay.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include "BonusList.h"
#include "../CStack.h"
#include <stdexcept>

void ProjectedBonusGraph::installRecipientOverlays(const CStack & original,
	std::vector<recipientBonusOverlay::Layer> layers)
{
	assignRecipientOverlays(original, std::move(layers), false);
}

void ProjectedBonusGraph::replayRecipientOverlays(const CStack & original,
	std::vector<recipientBonusOverlay::Layer> layers)
{
	assignRecipientOverlays(original, std::move(layers), true);
}

void ProjectedBonusGraph::assignRecipientOverlays(const CStack & original,
	std::vector<recipientBonusOverlay::Layer> layers, bool replacing)
{
	requireUsable();
	const auto binding = bindings.find(&original);
	if(binding == bindings.end() || !hasExactView(original, *binding->second.unit)
		|| recipientOverlays.contains(&original) != replacing || layers.size() > 512)
		throw std::invalid_argument("Recipient layers require an exact fresh bound occurrence");
	size_t remaining = 8192;
	for(const auto & layer : layers)
	{
		for(const auto * records : {&layer.added, &layer.updated})
		{
			if(records->size() > remaining || std::ranges::any_of(*records, [](const auto & record) { return !record; }))
				throw std::invalid_argument("Invalid recipient layer records");
			remaining -= records->size();
		}
		if(layer.removed.size() > remaining || layer.removed.contains(nullptr))
			throw std::invalid_argument("Invalid recipient removal identities");
		remaining -= layer.removed.size();
		if(layer.stackedSnapshot)
		{
			if(layer.stackedSnapshot->size() > remaining)
				throw std::length_error("Recipient snapshot exceeds its bound");
			remaining -= layer.stackedSnapshot->size();
		}
		if(layer.unstackedSnapshot)
		{
			if(layer.unstackedSnapshot->size() > remaining
				|| std::ranges::any_of(*layer.unstackedSnapshot, [](const auto & record) { return !record; }))
				throw std::invalid_argument("Invalid recipient raw snapshot");
			remaining -= layer.unstackedSnapshot->size();
		}
	}
	recipientOverlays.insert_or_assign(&original, std::move(layers));
	// Existing finalization/edge-edit admission refuses unjournaled query-only
	// state. A legacy recipient ADD must never become an invented native export.
	unjournaledReplacement = true;
}

std::optional<std::vector<recipientBonusOverlay::Layer>> ProjectedBonusGraph::removeRecipientOverlaysMatching(
	const CStack & original, const CSelector & selector)
{
	requireUsable();
	const auto found = recipientOverlays.find(&original);
	if(found == recipientOverlays.end())
		return std::nullopt;
	auto replacement = found->second;
	for(auto & layer : replacement)
	{
		const auto matches = [&](const auto & record) { return selector(record.get()); };
		std::erase_if(layer.added, matches);
		std::erase_if(layer.updated, matches);
		if(layer.stackedSnapshot)
			std::erase_if(*layer.stackedSnapshot, [&](const Bonus & record) { return selector(&record); });
		if(layer.unstackedSnapshot)
			std::erase_if(*layer.unstackedSnapshot, matches);
	}
	// Keep snapshot presence/groups and removal identities: removing a selected
	// snapshot entry must not resurrect a previously captured inherited record.
	replayRecipientOverlays(original, replacement);
	return replacement;
}

bool ProjectedBonusGraph::hasUnresolvedRecipientOverlays() const
{
	requireUsable();
	for(const auto & [source, layers] : recipientOverlays)
		if(std::ranges::any_of(layers, [](const auto & layer) { return layer.hasEdits(); }))
			return true;
	return false;
}

TConstBonusListPtr ProjectedBonusGraph::queryRecipientLayer(const CStack & original, size_t count,
	const CSelector & selector, bool unstacked)
{
	if(count == 0)
		return queryOriginal(original, selector, unstacked);
	const auto view = recipientOverlays.at(&original).at(count - 1).view();
	const auto & mergeSelector = view.updated.empty() ? selector : Selector::all;
	const auto inherited = queryRecipientLayer(original, count - 1, mergeSelector, unstacked);
	auto result = std::make_shared<BonusList>();
	recipientBonusOverlay::copyInherited(*result, *inherited, view, unstacked);
	recipientBonusOverlay::append(*result, view, mergeSelector, unstacked);
	return recipientBonusOverlay::finish(result, view, selector);
}
