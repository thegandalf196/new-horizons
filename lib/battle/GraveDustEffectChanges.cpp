/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GraveDustEffectChanges.h"
#include "NewHorizonsGraveDust.h"
#include "IBattleState.h"
#include "Unit.h"
#include "StackEffectRemoval.h"
#include "../bonuses/BonusList.h"
#include "../networkPacks/SetStackEffect.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace newHorizonsGraveDust
{
namespace
{
void invalidChange()
{
	throw std::invalid_argument("Invalid Grave Dust effect change");
}
class ApplicationState
{
	const IBattleInfo & battle;
	const battle::Unit & unit;
	std::vector<Bonus> current;
	void checkComplete() const
	{
		if(!current.empty()) validateSourceGroup(current);
	}
	void incoming(const Bonus & record, const EffectOperation operation)
	{
		if(!unit.alive() || unit.isGhost() || unit.isTimeStopped()) invalidChange();
		if(record.spellCasterOwner != battle.getSidePlayer(BattleSide::ATTACKER)
			&& record.spellCasterOwner != battle.getSidePlayer(BattleSide::DEFENDER)) invalidChange();
		const auto sameType = [&record](const Bonus & candidate) { return candidate.type == record.type; };
		const auto existing = std::find_if(current.begin(), current.end(), sameType);
		if(record.type == BonusType::PHYSICAL_AFFLICTION)
		{
			if(existing != current.end()) *existing = record;
			else current.push_back(record);
		}
		else if(operation == EffectOperation::UPDATE && existing != current.end())
		{
			// Native/ordinary projection UPDATE refreshes timers/tags, never strength/caster.
			if(existing->val != record.val || existing->spellCasterOwner != record.spellCasterOwner) invalidChange();
		}
		else current.push_back(record); // Native force-ADD never replaces an identified companion.
	}
public:
	ApplicationState(const IBattleInfo & battle, const battle::Unit & unit) : battle(battle), unit(unit)
	{
		const auto actual = unit.getUnstackedBonuses(CSelector([](const Bonus * bonus)
		{
			return bonus && isReservedSource(*bonus);
		}));
		if(!actual) invalidChange();
		for(const auto & bonus : *actual)
		{
			if(!bonus) invalidChange();
			current.push_back(*bonus);
		}
		checkComplete();
	}
	void apply(const std::vector<Bonus> & records, const EffectOperation operation)
	{
		if(!containsEffect(records)) return;
		if(operation == EffectOperation::REMOVE && unit.isTimeStopped()) return; // Native protected no-op.
		bool markerSeen = false;
		bool reductionSeen = false;
		for(const auto & record : records)
		{
			if(!validateRecord(record)) continue;
			if(operation != EffectOperation::REMOVE)
			{
				auto & seen = record.type == BonusType::PHYSICAL_AFFLICTION ? markerSeen : reductionSeen;
				if(seen) invalidChange();
				seen = true;
			}
			if(operation == EffectOperation::REMOVE)
				std::erase_if(current, [&record](const Bonus & candidate)
				{
					return battle::matchesStackEffectRemoval(record, candidate);
				});
			else incoming(record, operation);
		}
		checkComplete();
	}
};
}
bool containsEffect(const std::vector<Bonus> & records)
{
	bool found = false;
	for(const auto & record : records) found = validateRecord(record) || found;
	return found;
}
bool containsEffect(const SetStackEffect & effects)
{
	bool found = false;
	for(const auto * changes : {&effects.toRemove, &effects.toUpdate, &effects.toAdd})
		for(const auto & entry : *changes) found = containsEffect(entry.second) || found;
	return found;
}
void validateMutation(const IBattleInfo & battle, const battle::Unit & unit,
	const std::vector<Bonus> & records, const EffectOperation operation)
{
	if(!containsEffect(records)) return;
	ApplicationState state(battle, unit);
	state.apply(records, operation);
}
void validateEffectChanges(const IBattleInfo & battle, const SetStackEffect & effects)
{
	if(!containsEffect(effects)) return;
	effects.validateExchange();
	std::set<uint32_t> targets;
	for(const auto * changes : {&effects.toRemove, &effects.toUpdate, &effects.toAdd})
		for(const auto & [id, records] : *changes)
			if(containsEffect(records)) targets.insert(id);
	for(const auto id : targets)
	{
		const auto units = battle.getUnitsIf([id](const battle::Unit * unit)
		{
			return unit && unit->unitId() == id;
		});
		if(units.size() != 1) invalidChange();
		ApplicationState state(battle, *units.front());
		const auto apply = [&state, id](const auto & changes, const EffectOperation operation)
		{
			for(const auto & entry : changes)
				if(entry.first == id) state.apply(entry.second, operation);
		};
		apply(effects.toRemove, EffectOperation::REMOVE);
		apply(effects.toUpdate, EffectOperation::UPDATE);
		apply(effects.toAdd, EffectOperation::ADD);
	}
}
}
