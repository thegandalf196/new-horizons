/*
 * ProjectedBonusGraphCleanup.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include <stdexcept>

void ProjectedBonusGraph::removeBonusesRecursive(const CBonusSystemNode & root, const CSelector & selector)
{
	requireUsable();
	try
	{
		synchronizeForms();
		validate();
		TCNodes active;
		removeBonusesRecursiveUnchecked(root, selector, active);
	}
	catch(...)
	{
		poisoned = true;
		throw;
	}
}

void ProjectedBonusGraph::removeBonusesRecursiveUnchecked(const CBonusSystemNode & root,
	const CSelector & selector, TCNodes & active, std::vector<ProjectedExportRemoval> * receipts)
{
	if(!active.insert(&root).second)
		throw std::logic_error("Projected cleanup contains a child cycle");
	// Same snapshot-before-removal and child-vector order as removeBonuses /
	// removeBonusesRecursive. Do not collect the whole DAG first or deduplicate
	// diamond paths: later selector calls observe earlier private removals.
	BonusList toRemove;
	values(root).exported.getBonuses(toRemove, selector);
	for(const auto & bonus : toRemove)
	{
		if(receipts)
			receipts->push_back({&root, bonus});
		removeCapturedExported(root, bonus);
	}
	for(const auto * child : children(root))
		removeBonusesRecursiveUnchecked(*child, selector, active, receipts);
	active.erase(&root);
}

std::vector<ProjectedExportRemoval> ProjectedBonusGraph::removeBonusesRecursiveRecording(
	const CBonusSystemNode & root, const CSelector & selector)
{
	requireUsable();
	try
	{
		synchronizeForms();
		validate();
		TCNodes active;
		std::vector<ProjectedExportRemoval> receipts;
		removeBonusesRecursiveUnchecked(root, selector, active, &receipts);
		return receipts;
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::nodeHasChanged(const CBonusSystemNode & root)
{
	requireUsable();
	try
	{
		synchronizeForms();
		validate();
		(void)values(root); // explicit captured authority before recording
		invalidations.record(true, &root, *this);
	}
	catch(...)
	{
		poisoned = true;
		throw;
	}
}
