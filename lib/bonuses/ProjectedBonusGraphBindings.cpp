/*
 * ProjectedBonusGraphBindings.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include "ProjectedBonusRoster.h"
#include "../CStack.h"
#include "../battle/CUnitState.h"
#include <stdexcept>

void ProjectedBonusGraph::bind(const CStack & original, const battle::CUnitState & unit,
	const ProjectedBonusRoster & roster)
{
	bindImpl(original, unit, roster, false);
}

void ProjectedBonusGraph::bindOccurrence(const CStack & original, const battle::CUnitState & unit,
	const ProjectedBonusRoster & roster)
{
	if(!roster.usesOrderedOccurrences())
		throw std::invalid_argument("Projected occurrence binding requires an explicit ordered roster");
	bindImpl(original, unit, roster, true);
}

void ProjectedBonusGraph::appendOccurrence(const CStack & original, const battle::CUnitState & unit,
	ProjectedBonusRoster & roster)
{
	requireUsable();
	try
	{
		roster.appendOccurrence(unit);
		bindOccurrence(original, unit, roster);
	}
	catch(...)
	{
		poisoned = true;
		throw;
	}
}

void ProjectedBonusGraph::bindImpl(const CStack & original, const battle::CUnitState & unit,
	const ProjectedBonusRoster & roster, bool occurrences)
{
	requireUsable();
	BonusEvaluationContext check(original, unit, roster);
	(void)check;
	if(!hasExactView(original, unit))
		throw std::invalid_argument("Projected binding requires this graph's exact original view");
	if(bindings.contains(&original))
		throw std::logic_error("Projected original already has a bound unit");
	for(const auto & [source, binding] : bindings)
		if(binding.unit == &unit || (!occurrences && source->unitId() == original.unitId()))
			throw std::invalid_argument("Projected unit identity is already bound");
	const auto * detached = dynamic_cast<const battle::CUnitStateDetached *>(&unit);
	if(occurrences && (!detached || !detached->usesUnitInfo(original)))
		throw std::invalid_argument("Projected occurrence requires its exact original descriptor");
	capture(original);
	bindings.emplace(&original, Binding{&unit, &roster, original.formBonusSource, occurrences,
		original.getBattleFormViewRevision(), unit.getBattleFormViewRevision(),
		original.formBonusSource, original.doubleWideCached, original.typeID});
}

void ProjectedBonusGraph::bindColdOccurrence(const CStack & original, const battle::CUnitState & unit,
	const ProjectedBonusRoster & roster, const BattleInfo * battle)
{
	requireUsable();
	if(original.base || original.getBattle() || !original.getParentNodes().empty()
		|| !liveBonusGraph().propagatingParents(original).empty() || !liveBonusGraph().children(original).empty())
		throw std::invalid_argument("Cold projection requires an unattached baseless native identity");
	bindOccurrence(original, unit, roster);
	bindings.at(&original).nativeBattle = battle;
}

void ProjectedBonusGraph::appendColdOccurrence(const CStack & original, const battle::CUnitState & unit,
	ProjectedBonusRoster & roster, const BattleInfo * battle)
{
	requireUsable();
	try
	{
		roster.appendOccurrence(unit);
		bindColdOccurrence(original, unit, roster, battle);
	}
	catch(...) { poisoned = true; throw; }
}
