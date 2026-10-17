/*
 * ProjectedBonusGraphDurations.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include "BonusDurationStep.h"
#include <stdexcept>

std::shared_ptr<Bonus> ProjectedBonusGraph::currentDurationRecord(std::shared_ptr<Bonus> record) const
{
	// Each edge was created by one preceding decrement. Retained old pointers
	// cannot be reused; this bound also detects an accidentally cyclic program.
	for(size_t count = 0; count <= durationAliases.size(); ++count)
	{
		const auto found = durationAliases.find(record);
		if(found == durationAliases.end())
			return record;
		record = found->second;
	}
	throw std::logic_error("Projected duration alias cycle");
}

void ProjectedBonusGraph::replaceDurationAliases(const std::shared_ptr<Bonus> & before,
	const std::shared_ptr<Bonus> & after)
{
	if(!before || !after || before == after || durationAliases.contains(before))
		throw std::invalid_argument("Duration replacement requires the current exact record identity");
	for(auto & [node, entry] : entries)
		for(auto * list : {&entry.current.accepted, &entry.current.exported})
			for(size_t index = 0; index < list->size(); ++index)
				if((*list)[index] == before)
					(*list)[index] = after;
	durationAliases.emplace(before, after);
	// Native decrement does not invalidate. Expiry below invokes the ordinary
	// native-equivalent unpropagation/invalidation, never re-propagates a value.
}

struct ProjectedBonusGraph::DurationStorage
{
	ProjectedBonusGraph & graph;
	const CBonusSystemNode & source;
	std::vector<ProjectedDurationChange> & changes;
	std::shared_ptr<Bonus> decrement(const std::shared_ptr<Bonus> & record)
	{
		const auto before = graph.currentDurationRecord(record);
		if(!vstd::contains(graph.values(source).exported, before))
			throw std::invalid_argument("Duration step requires its actual exporting source");
		auto after = std::make_shared<Bonus>(*before);
		after->turnsRemain--;
		graph.replaceDurationAliases(before, after);
		changes.push_back({&source, before, after, false});
		return after;
	}
	void remove(const std::shared_ptr<Bonus> & record)
	{
		changes.back().removed = true;
		graph.removeCapturedExported(source, record);
	}
};

void ProjectedBonusGraph::reduceDurationsUnchecked(const CBonusSystemNode & source,
	const CSelector & selector, std::vector<ProjectedDurationChange> & changes, TCNodes & active)
{
	if(!active.insert(&source).second)
		throw std::logic_error("Projected duration traversal contains a child cycle");
	DurationStorage storage{*this, source, changes};
	bonusDuration::reduceSelected(values(source).exported, selector, storage);
	for(const auto * child : children(source))
		reduceDurationsUnchecked(*child, selector, changes, active);
	active.erase(&source); // Preserve duplicate diamond visits, not global dedup.
}

std::vector<ProjectedDurationChange> ProjectedBonusGraph::reduceDurationsRecording(
	const CBonusSystemNode & source, const CSelector & selector)
{
	requireUsable();
	try
	{
		synchronizeForms();
		validate();
		std::vector<ProjectedDurationChange> changes;
		TCNodes active;
		reduceDurationsUnchecked(source, selector, changes, active);
		return changes;
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::replayDurationChanges(const std::vector<ProjectedDurationChange> & changes)
{
	requireUsable();
	try
	{
		synchronizeForms();
		validate();
		for(const auto & change : changes)
		{
			if(!change.before)
				throw std::invalid_argument("Duration replay has no preceding value");
			auto expectedDuration = change.before->turnsRemain;
			--expectedDuration; // Same field type/conversion as the native decrement.
			if(!change.source || currentDurationRecord(change.before) != change.before
				|| !change.after || change.after->turnsRemain != expectedDuration
				|| change.removed != (change.after->turnsRemain <= 0))
				throw std::invalid_argument("Duration replay requires its exact ordered source/value receipt");
			(void)values(*change.source);
			if(!vstd::contains(values(*change.source).exported, change.before))
				throw std::invalid_argument("Duration replay source does not export this record");
			replaceDurationAliases(change.before, change.after);
			if(change.removed)
				removeCapturedExported(*change.source, change.after);
		}
	}
	catch(...) { poisoned = true; throw; }
}
