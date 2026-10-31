/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameStatePackVisitor.h"
#include "../battle/GraveDustEffectChanges.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../battle/CUnitState.h"
#include "../battle/NewHorizonsSerpentsEmbrace.h"
#include "../spells/HydrasVitalityIdentity.h"
#include "../networkPacks/SetStackEffect.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include <algorithm>
#include <set>

void BattleStatePackVisitor::visitSetStackEffect(SetStackEffect & pack)
{
	pack.validateExchange();
	if(pack.exchange)
	{
		battleState.exchangeBattleEffects(*pack.exchange);
		return;
	}
	newHorizonsGraveDust::validateEffectChanges(battleState, pack);
	pack.validateConfusionMarkers();
	pack.validateGazeApplication(battleState);
	// Validate the entire new-restraint batch before any removal or update.
	for(const auto * changes : {&pack.toAdd, &pack.toUpdate})
		for(const auto & [target, bonuses] : *changes)
			for(const auto & bonus : bonuses)
				newHorizonsSerpentsEmbrace::validateApplication(battleState, target, bonus);
	const auto hydrasVitalitySource = newHorizonsMagic::hydrasVitalitySource(battleState.getMagicRules());
	std::set<uint32_t> capacityAffectedStacks;
	auto collectCapacityChanges = [&capacityAffectedStacks, &hydrasVitalitySource](const auto & changes)
	{
		for(const auto & stackEffects : changes)
			if(std::ranges::any_of(stackEffects.second, [&hydrasVitalitySource](const Bonus & bonus)
			{
				return bonus.type == BonusType::STACK_HEALTH
					|| (bonus.type == BonusType::HP_REGENERATION
						&& hydrasVitalitySource(&bonus));
			}))
				capacityAffectedStacks.insert(stackEffects.first);
	};
	collectCapacityChanges(pack.toRemove);
	collectCapacityChanges(pack.toUpdate);
	collectCapacityChanges(pack.toAdd);

	for(const auto & stackData : pack.toRemove)
		battleState.removeUnitBonus(stackData.first, stackData.second);

	for(const auto & stackData : pack.toUpdate)
		battleState.updateUnitBonus(stackData.first, stackData.second);

	for(const auto & stackData : pack.toAdd)
		battleState.addUnitBonus(stackData.first, stackData.second);

	if(capacityAffectedStacks.empty())
		return;
	const auto hydrasCapacity = hydrasVitalitySource.And(Selector::type()(BonusType::STACK_HEALTH));
	const auto hydrasRegeneration = hydrasVitalitySource.And(Selector::type()(BonusType::HP_REGENERATION));
	for(const auto * unit : battleState.getUnitsIf([&capacityAffectedStacks](const battle::Unit * candidate)
		{
			return candidate && capacityAffectedStacks.contains(candidate->unitId());
		}))
	{
		auto state = unit->acquireState();
		if(!state->health.isCapacityHealthTracking())
			continue;
		state->normalizeCapacityHealth();
		if(!unit->hasBonus(hydrasCapacity) && !unit->hasBonus(hydrasRegeneration))
			state->clearCapacityHealthReference();
		battleState.updateUnit(unit->unitId(), state->save(), 0);
	}
}
