/*
 * ProjectedMeleeExpiry.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedMeleeExpiry.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/StackEffectRemoval.h"
#include "../../lib/bonuses/Propagators.h"

namespace
{
bool boundedStorage(const std::map<uint32_t, MeleeBonusSource> & sources)
{
	std::set<const CBonusSystemNode *> seen;
	std::vector<const CBonusSystemNode *> work;
	for(const auto & [id, source] : sources)
		work.push_back(source.native);
	size_t edges = 0, records = 0;
	const auto & access = liveBonusGraph();
	while(!work.empty())
	{
		const auto * node = work.back();
		work.pop_back();
		if(!node)
			return false;
		if(!seen.insert(node).second)
			continue;
		if(seen.size() > 4096)
			return false;
		for(const auto * list : {&access.local(*node), &node->getExportedBonusList()})
			for(const auto & record : *list)
				if(!record || ++records > 65536 || (record->propagator
					&& typeid(*record->propagator) != typeid(IPropagator)
					&& typeid(*record->propagator) != typeid(CPropagatorNodeType)))
					return false;
		const auto append = [&](const auto & next)
		{
			if(next.size() > 16384 - edges)
				return false;
			edges += next.size();
			work.insert(work.end(), next.begin(), next.end());
			return true;
		};
		if(!append(access.parents(*node)) || !append(access.propagatingParents(*node)) || !append(access.children(*node)))
			return false;
	}
	return true; // Storage bound only; shared graph still validates DAG semantics.
}
}

bool ProjectedMeleeExpiry::capture(HypotheticBattle & battle)
{
	if(auto subject = battle.initialExportSubject())
	{
		if(subject->world().hasUnresolvedRecipientOverlays())
			return false; // After movement/count/sequence-input capture, before ANY primary injury.
		// The existing owned subject already has complete originating history and
		// canonical recipient bindings. Never build another expiry graph for it.
		subject->synchronizeValues(battle);
		branch = &battle;
		ownedSubject = std::move(subject);
		return true;
	}
	const auto roster = battle.battleGetUnitsIf([](const battle::Unit *) { return true; });
	if(roster.size() > 256)
		return false;
	for(const auto * unit : roster)
	{
		if(!unit || sources.contains(unit->unitId()))
			return false;
		auto state = battle.getForUpdate(unit->unitId());
		MeleeBonusSource source;
		if(!state->captureMeleeBonusSource(source))
			return false;
		sources.emplace(unit->unitId(), std::move(source));
		units.emplace(unit->unitId(), std::move(state));
	}
	if(!boundedStorage(sources))
		return false;
	for(const auto & [id, source] : sources)
		graph.capture(*source.native);
	for(const auto & [id, source] : sources)
		for(const auto & record : source.localExports)
			graph.addExported(*source.native, record);
	return true;
}

bool ProjectedMeleeExpiry::captureRecipientLayers(HypotheticBattle & battle)
{
	auto subject = battle.initialExportSubject();
	if(!subject)
		return false;
	// Capture the same branch's actual current inputs, after movement/count and
	// sequence-expiry inputs. Never infer native exports from its effective list.
	subject->captureBranchValues(battle);
	if(!subject->world().hasRecordedRecipientInjuryInputs())
		return false;
	if(!subject->world().hasUnresolvedRecipientOverlays())
		return capture(battle); // Positive clean empty-history classification, not error fallback.
	branch = &battle;
	ownedSubject = std::move(subject);
	return true;
}

std::map<uint32_t, TConstBonusListPtr> ProjectedMeleeExpiry::views()
{
	std::map<uint32_t, TConstBonusListPtr> result;
	for(const auto & [id, source] : sources)
		result.emplace(id, graph.query(*source.native, Selector::all, true));
	return result;
}

void ProjectedMeleeExpiry::publishRemoved(const std::map<uint32_t, TConstBonusListPtr> & before)
{
	const auto after = views();
	for(const auto & [id, old] : before)
	{
		std::set<std::shared_ptr<Bonus>> removed;
		for(const auto & record : *old)
			if(!vstd::contains(*after.at(id), record))
				removed.insert(record);
		if(!removed.empty())
			units.at(id)->suppressMeleeBonusIdentities(removed);
	}
}

void ProjectedMeleeExpiry::expire(uint32_t actorId, const CSelector & selector)
{
	if(ownedSubject)
	{
		ownedSubject->remove(*branch, actorId, selector);
		return;
	}
	const auto before = views();
	graph.removeBonusesRecursive(*sources.at(actorId).native, selector);
	publishRemoved(before);
}

void ProjectedMeleeExpiry::expireSelected(uint32_t actorId, const std::vector<Bonus> & selected)
{
	for(const auto & record : selected)
		expire(actorId, CSelector([&record](const Bonus * candidate)
		{
			return battle::matchesStackEffectRemoval(record, *candidate);
		}));
}
