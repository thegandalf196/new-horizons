/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GraveDustProjection.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "../../lib/battle/GraveDustOrderedPreflight.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/GraveDustEffectChanges.h"
#include "../../lib/bonuses/BonusList.h"
void graveDustAI::validateSelectorRemoval(const IBattleInfo & battle, const battle::Unit & unit, const CSelector & requested)
{
	if(unit.isTimeStopped()) return;
	std::vector<Bonus> selected;
	const auto graveDustEffects = unit.getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect));
	for(const auto & bonus : *graveDustEffects)
		if(requested(bonus.get())) selected.push_back(*bonus);
	newHorizonsGraveDust::validateMutation(battle, unit, selected, newHorizonsGraveDust::EffectOperation::REMOVE);
}
CSelector graveDustAI::protectedRemovalSelector(const battle::Unit & unit, const CSelector & requested)
{
	if(!unit.isTimeStopped()) return requested;
	return [requested](const Bonus * bonus)
	{
		return requested(bonus) && !newHorizonsGraveDust::isEffect(bonus);
	};
}

void HypotheticBattle::validateOrdinaryEffectChanges(const SetStackEffect & effects,
	const StackWithBonuses * currentOverride) const
{
	if(replayingOrdinaryEffects || initialExportSubject() || usesOwnedFrameQueries()
		|| !newHorizonsGraveDust::containsEffect(effects)) return;
	// A synchronous ordinary child has the established query semantics and no
	// authority to publish. This lexical borrow never escapes the private replay.
	const Subject borrowed(const_cast<HypotheticBattle *>(this), [](CBattleInfoCallback *) {});
	HypotheticBattle preview(env, borrowed);
	preview.replayingOrdinaryEffects = true;
	// Every query dependency must resolve a preview-owned current unit. Do not
	// leave untouched peers pointing at the parent's mutable scalar state.
	for(const auto * unit : battleGetUnitsIf([](const battle::Unit *) { return true; }))
		preview.getForUpdate(unit->unitId());
	if(currentOverride)
		preview.stackStates[currentOverride->unitId()] = std::make_shared<StackWithBonuses>(
			&preview, static_cast<const battle::CUnitState *>(currentOverride));
	newHorizonsGraveDust::validateOrderedEffectChanges(preview, effects,
		[&](uint32_t id) { return preview.battleGetUnitByID(id); },
		[&](uint32_t id, const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation)
		{
			if(!preview.battleGetUnitByID(id)) return; // Existing ordinary absent-target policy.
			if(!currentOverride)
			{
				// Match the actual packet's IBattleState dispatch: form restoration
				// and battle-wide invalidation belong to these established wrappers.
				if(operation == newHorizonsGraveDust::EffectOperation::REMOVE) preview.removeUnitBonus(id, records);
				else if(operation == newHorizonsGraveDust::EffectOperation::UPDATE) preview.updateUnitBonus(id, records);
				else preview.addUnitBonus(id, records);
				return;
			}
			auto unit = preview.getForUpdate(id);
			if(operation == newHorizonsGraveDust::EffectOperation::REMOVE) unit->removeUnitBonus(records);
			else if(operation == newHorizonsGraveDust::EffectOperation::UPDATE) unit->updateUnitBonus(records);
			else unit->addUnitBonus(records);
		});
}

void HypotheticBattle::validateOrdinaryEffectVector(const StackWithBonuses & unit,
	const std::vector<Bonus> & records, newHorizonsGraveDust::EffectOperation operation) const
{
	if(replayingOrdinaryEffects || !newHorizonsGraveDust::containsEffect(records)) return;
	if(ordinaryValidatedPacket)
	{
		const auto found = stackStates.find(unit.unitId());
		const auto & rows = operation == newHorizonsGraveDust::EffectOperation::REMOVE
			? ordinaryValidatedPacket->toRemove : operation == newHorizonsGraveDust::EffectOperation::UPDATE
			? ordinaryValidatedPacket->toUpdate : ordinaryValidatedPacket->toAdd;
		// Only this exact real visitor row/occurrence is covered by packet admission.
		if(found != stackStates.end() && found->second.get() == &unit
			&& std::ranges::any_of(rows, [&](const auto & row)
			{
				return row.first == unit.unitId() && &row.second == &records;
			})) return;
	}
	SetStackEffect changes; changes.battleID = getBattleID();
	auto & rows = operation == newHorizonsGraveDust::EffectOperation::REMOVE ? changes.toRemove
		: operation == newHorizonsGraveDust::EffectOperation::UPDATE ? changes.toUpdate : changes.toAdd;
	rows.emplace_back(unit.unitId(), records);
	validateOrdinaryEffectChanges(changes, &unit);
}

void HypotheticBattle::preflightEffectChanges(const SetStackEffect & effects) const
{
	if(ordinaryValidatedPacket == &effects || !newHorizonsGraveDust::containsEffect(effects)) return;
	if(initialExportSubject() || usesOwnedFrameQueries())
		newHorizonsGraveDust::validateEffectChanges(*this, effects); // Retain these existing visitor policies.
	else
		validateOrdinaryEffectChanges(effects);
}
