/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GraveDustNativePreflight.h"
#include "GraveDustOrderedPreflight.h"
#include "ScopedStackEffectQueries.h"
#include "ScopedStackEffectReplay.h"
#include "BattleInfo.h"
#include "../CStack.h"
#include <set>
#include <stdexcept>
namespace newHorizonsGraveDust
{
void validateNativeEffectChanges(const BattleInfo & battle, const SetStackEffect & effects)
{
	if(!containsEffect(effects)) return;
	effects.validateExchange();
	std::set<uint32_t> requested;
	for(const auto * changes : {&effects.toRemove, &effects.toUpdate, &effects.toAdd})
		for(const auto & [id, records] : *changes) requested.insert(id);
	std::vector<const CStack *> targets;
	std::set<uint32_t> present;
	for(const auto * unit : battle.getUnitsIf([&](const battle::Unit * unit)
	{
		return unit && requested.contains(unit->unitId());
	}))
	{
		const auto * stack = dynamic_cast<const CStack *>(unit);
		if(!stack) throw std::invalid_argument("Invalid native Grave Dust target");
		targets.push_back(stack); present.insert(unit->unitId());
	}
	battle::ScopedStackEffectQueries queries(battle, targets);
	validateOrderedEffectChanges(battle, effects,
		[&](uint32_t id) -> const battle::Unit *
		{
			return present.contains(id) ? &queries.current(id) : nullptr;
		},
		[&](uint32_t id, const std::vector<Bonus> & records, EffectOperation operation)
		{
			battle::replayScopedStackEffectVector(queries, battle, id, records, operation);
		});
}
void validateEffectChanges(const BattleInfo & battle, const SetStackEffect & effects)
{
	validateNativeEffectChanges(battle, effects);
}
void validateNativeMutation(const BattleInfo & battle, const CStack & unit,
	const std::vector<Bonus> & records, EffectOperation operation)
{
	if(!containsEffect(records)) return;
	const auto occurrences = battle.getUnitsIf([&unit](const battle::Unit * candidate) { return candidate == &unit; });
	if(unit.getBattle() != &battle || occurrences.size() != 1)
		throw std::invalid_argument("Invalid native Grave Dust target occurrence");
	SetStackEffect effects;
	auto & changes = operation == EffectOperation::REMOVE ? effects.toRemove
		: operation == EffectOperation::UPDATE ? effects.toUpdate : effects.toAdd;
	changes.emplace_back(unit.unitId(), records);
	validateNativeEffectChanges(battle, effects);
}
}
