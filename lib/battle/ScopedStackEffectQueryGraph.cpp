/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ScopedStackEffectQueryData.h"
#include "../CStack.h"
#include "../bonuses/BonusLimitation.h"
#include "../bonuses/BonusSourceRelationRecipe.h"
#include "../bonuses/BonusSelector.h"
#include "../CCreatureHandler.h"
#include <algorithm>

namespace battle
{
std::shared_ptr<Bonus> ScopedStackEffectQueries::Data::copy(const std::shared_ptr<Bonus> & value) const
{
	if(!value)
		throw std::invalid_argument("Null scoped stack-effect record");
	auto [entry, inserted] = aliases.try_emplace(value.get());
	if(inserted)
	{
		entry->second.input = value; // Retain temporary inputs against address reuse.
		entry->second.value = std::make_shared<Bonus>(*value);
	}
	return entry->second.value;
}

ScopedStackEffectQueries::Data::Lists & ScopedStackEffectQueries::Data::storage(const CBonusSystemNode & node) const
{
	auto [entry, inserted] = lists.try_emplace(&node);
	if(inserted)
	{
		for(const auto & value : liveBonusGraph().local(node))
			entry->second.local.push_back(copy(value));
		for(const auto & value : node.getExportedBonusList())
			entry->second.exported.push_back(copy(value));
	}
	return entry->second;
}

const BonusList & ScopedStackEffectQueries::Data::local(const CBonusSystemNode & node) const { return storage(node).local; }
const TCNodesVector & ScopedStackEffectQueries::Data::parents(const CBonusSystemNode & node) const
{
	const auto found = parentChanges.find(&node);
	return found == parentChanges.end() ? liveBonusGraph().parents(node) : found->second;
}
const TNodesVector & ScopedStackEffectQueries::Data::propagatingParents(const CBonusSystemNode & node) const { return liveBonusGraph().propagatingParents(node); }
const TNodesVector & ScopedStackEffectQueries::Data::children(const CBonusSystemNode & node) const { return liveBonusGraph().children(node); }

BonusEvaluationContext ScopedStackEffectQueries::Data::evaluation(const CBonusSystemNode & node) const
{
	if(const auto * stack = dynamic_cast<const CStack *>(&node))
		if(const auto found = rows.find(stack); found != rows.end())
			return BonusEvaluationContext(*stack, found->second->state, *roster);
	return liveBonusGraph().evaluation(node);
}

TConstBonusListPtr ScopedStackEffectQueries::Data::query(const Row & row, const CSelector & selector, bool unstacked) const
{
	BonusList collected;
	collectBonusGraph(*this, row.source, collected);
	BonusList limited;
	limitBonuses(collected, limited, evaluation(row.source));
	if(!unstacked)
		limited.stackBonuses();
	auto result = std::make_shared<BonusList>();
	limited.getBonuses(*result, selector);
	return result;
}

void ScopedStackEffectQueries::Data::propagate(const CBonusSystemNode & destination,
	const std::shared_ptr<Bonus> & value, const CBonusSystemNode & source, bool add)
{
	if(add)
		appendPropagatedArrival(storage(destination).local, value, destination, evaluation(source));
	else
		removePropagatedArrival(storage(destination).local, value, destination);
	TCNodes next;
	bonusRedChildren(*this, destination, next);
	for(const auto * child : next)
		propagate(*child, value, source, add);
}

void ScopedStackEffectQueries::Data::remove(const CBonusSystemNode & node, const std::shared_ptr<Bonus> & value)
{
	storage(node).exported -= value;
	if(value->propagator)
		propagate(node, value, node, false);
	else
		storage(node).local -= value;
}

void ScopedStackEffectQueries::Data::removeMatching(const CBonusSystemNode & node, const CSelector & selector)
{
	BonusList selected;
	storage(node).exported.getBonuses(selected, selector);
	for(const auto & value : selected)
		remove(node, value);
	for(const auto * child : children(node))
		removeMatching(*child, selector);
}

void ScopedStackEffectQueries::Data::changeForm(Row & row)
{
	if(row.initializing)
		return;
	const CCreature * desired = nullptr;
	if(row.state.hasBattleForm() && row.state.battleFormCreature() != row.baseCreature)
		desired = row.state.battleFormCreature().toCreature();
	if(row.attached == desired)
		return;
	auto [entry, inserted] = parentChanges.try_emplace(&row.source);
	if(inserted)
		entry->second = liveBonusGraph().parents(row.source);
	auto & currentParents = entry->second;
	const auto relation = [&](const CCreature & source, bool add)
	{
		if(row.source.isHypothetic()) return;
		forEachPropagatingSourceRecord(*this, source,
			[this](const CBonusSystemNode & node) -> const BonusList & { return storage(node).exported; },
			[&](const std::shared_ptr<Bonus> & value) { propagate(row.source, value, source, add); });
	};
	if(row.attached)
	{
		relation(*row.attached, false); // native detaches arrivals before parent erasure
		std::erase(currentParents, row.attached);
	}
	row.attached = desired;
	if(row.attached)
	{
		currentParents.push_back(row.attached); // native attaches edge before arrivals
		relation(*row.attached, true);
	}
}
}
