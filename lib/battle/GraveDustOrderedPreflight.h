/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "GraveDustEffectChanges.h"
#include "../networkPacks/SetStackEffect.h"
#include <stdexcept>
namespace newHorizonsGraveDust
{
/// Validation-only replay over a caller-owned PRIVATE native/ledger scope.
/// current(id) returns its current Unit or null for an absent native target.
/// replay(id,records,operation) performs one exact private vector mutation,
/// including its entry/admission and preparation/completion boundaries. Never pass a live
/// writer: failure discards the scope, with no source/packet publication.
template<typename Current, typename Replay>
void validateOrderedEffectChanges(const IBattleInfo & battle, const SetStackEffect & effects,
	Current && current, Replay && replay)
{
	if(!containsEffect(effects)) return;
	effects.validateExchange();
	for(const auto operation : {EffectOperation::REMOVE, EffectOperation::UPDATE, EffectOperation::ADD})
	{
		const auto & changes = operation == EffectOperation::REMOVE ? effects.toRemove
			: operation == EffectOperation::UPDATE ? effects.toUpdate : effects.toAdd;
		for(const auto & [id, records] : changes)
		{
			const bool dust = containsEffect(records);
			const auto * before = current(id);
			if(!before && dust) throw std::invalid_argument("Invalid Grave Dust effect change");
			// Replay retains vector-entry validation even for absent ordinary targets.
			replay(id, records, operation);
			if(dust)
			{
				const auto * after = current(id);
				if(!after) throw std::invalid_argument("Invalid Grave Dust effect change");
				validateInstalledGroup(*after);
			}
		}
	}
}
}
