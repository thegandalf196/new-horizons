/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitShieldState.h"
#include "CUnitState.h"
#include "ConsecratedArmsOrder.h"
#include "IBattleState.h"
#include "../networkPacks/SetStackEffect.h"
#include <algorithm>
#include <set>
namespace battle
{
void synchronizePrayerEffects(IBattleState & state, const SetStackEffect & effects)
{
	std::set<uint32_t> affected;
	for(const auto * changes : {&effects.toRemove, &effects.toUpdate, &effects.toAdd})
		for(const auto & [target, bonuses] : *changes)
			if(std::ranges::any_of(bonuses, [](const Bonus & bonus)
			{ return bonus.type == BonusType::NONE && consecratedArms::identity(&bonus).has_value(); }))
				affected.insert(target);
	if(affected.empty()) return;
	// Never clear the pool between a refresh's removal and addition stages.
	for(const auto * unit : state.getUnitsIf([&affected](const Unit * candidate)
		{ return candidate && affected.contains(candidate->unitId()); }))
	{
		consecratedArms::refreshDivinePurpose(state, *unit);
		auto value = unit->acquireState();
		value->synchronizeRedemptionShield();
		state.updateUnit(unit->unitId(), value->save(), 0);
	}
}
}
