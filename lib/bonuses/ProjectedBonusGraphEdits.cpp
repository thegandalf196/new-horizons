/*
 * ProjectedBonusGraphEdits.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "ProjectedBonusGraph.h"
#include "Propagators.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include <stdexcept>

void ProjectedBonusGraph::propagate(const CBonusSystemNode & destination, const CBonusSystemNode & source,
	const std::shared_ptr<Bonus> & bonus, bool remove)
{
	auto & accepted = entries.at(&destination).current.accepted;
	if(remove)
	{
		if(bonus->propagator->shouldBeAttached(const_cast<CBonusSystemNode *>(&destination)))
		{
			removePropagatedBonus(accepted, bonus);
			invalidations.record(true, &destination, *this);
		}
	}
	else if(auto propagated = propagatedBonus(bonus, destination, evaluation(source)))
	{
		accepted.push_back(propagated);
		invalidations.record(true, &destination, *this);
	}
	TCNodes next;
	bonusRedChildren(*this, destination, next);
	for(const auto * child : next)
		propagate(*child, source, bonus, remove);
}

void ProjectedBonusGraph::addExported(const CBonusSystemNode & source, const std::shared_ptr<Bonus> & record)
{
	requireUsable();
	synchronizeForms();
	validate();
	const auto bonus = currentDurationRecord(record);
	if(!bonus || vstd::contains(values(source).exported, bonus))
		throw std::invalid_argument("Projected exported bonus is null or already owned");
	if((Bonus::NTurns(bonus.get()) || Bonus::NDays(bonus.get()) || Bonus::OneWeek(bonus.get())) && !bonus->turnsRemain)
		throw std::invalid_argument("Projected timed bonus has no remaining duration");
	try
	{
		entries.at(&source).current.exported.push_back(bonus);
		if(bonus->propagator)
			propagate(source, source, bonus, false);
		else
		{
			entries.at(&source).current.accepted.push_back(bonus);
			invalidations.record(true, &source, *this);
		}
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::removeExported(const CBonusSystemNode & source, const std::shared_ptr<Bonus> & bonus)
{
	requireUsable();
	synchronizeForms();
	validate();
	const auto current = currentDurationRecord(bonus);
	if(!current || !vstd::contains(values(source).exported, current))
		throw std::invalid_argument("Projected exported bonus is not owned");
	removeCapturedExported(source, current);
}

void ProjectedBonusGraph::removeCapturedExported(const CBonusSystemNode & source,
	const std::shared_ptr<Bonus> & bonus)
{
	try
	{
		entries.at(&source).current.exported -= bonus;
		if(bonus->propagator)
			propagate(source, source, bonus, true);
		else
		{
			entries.at(&source).current.accepted -= bonus;
			invalidations.record(true, &source, *this);
		}
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::synchronizeForm(const CStack & original)
{
	requireUsable();
	try
	{
		auto & binding = bindings.at(&original);
		const CCreature * desired = nullptr;
		if(binding.unit->hasBattleForm() && binding.unit->battleFormCreature() != original.typeID)
			desired = binding.unit->battleFormCreature().toCreature();
		if(binding.formSource == desired)
			return;
		if(desired)
			capture(*desired);
		validate(); // no recursive walk until both DAGs/endpoints are checked
		if(binding.formSource)
			sourceRelation(original, *binding.formSource, false);
		if(desired)
			sourceRelation(original, *desired, true);
		binding.formSource = desired;
	}
	catch(...) { poisoned = true; throw; }
}

void ProjectedBonusGraph::synchronizeForms()
{
	const CStack * pending = nullptr;
	for(const auto & [original, binding] : bindings)
	{
		const CCreature * desired = nullptr;
		if(binding.unit->hasBattleForm() && binding.unit->battleFormCreature() != original->typeID)
			desired = binding.unit->battleFormCreature().toCreature();
		if(binding.formSource == desired)
			continue;
		if(pending)
			throw std::logic_error("Synchronize pending projected forms in their explicit authored order");
		pending = original;
	}
	if(pending)
		synchronizeForm(*pending);
}
