/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackEffectRecordRules.h"
#include "CUnitState.h"
#include "BattleForm.h"
#include "NewHorizonsSwiftRebirth.h"
#include "NewHorizonsConfusionControl.h"
#include "NewHorizonsOffense.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "TimeStopState.h"
#include "StackEffectRemoval.h"
#include "../bonuses/BonusList.h"
#include <algorithm>
#include <stdexcept>
namespace stackEffectRecordRules
{
bool incomingProtected(const battle::Unit & unit, const Bonus & value)
{
	return unit.isTimeStopped() && !timeStopState::isStateBonus(value);
}
bool removalProtected(const battle::Unit & unit, const Bonus & value)
{
	return unit.isTimeStopped() && !timeStopState::isStateBonus(value)
		&& !newHorizonsOffense::isNoQuarterBonus(&value)
		&& !newHorizonsSerpentsEmbrace::isMarker(value);
}
std::shared_ptr<Bonus> previousSwiftLifecycle(const battle::Unit & unit, const Bonus & value,
	bool forceAdd, const BonusList & localExports)
{
	newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(unit), value, forceAdd);
	std::shared_ptr<Bonus> previous;
	for(const auto & existing : localExports)
		if(existing && newHorizonsSwiftRebirth::isLifecycleMarker(*existing)) previous = existing;
	if(!forceAdd && !previous)
		throw std::invalid_argument("Swift Rebirth update requires a local lifecycle");
	return previous;
}
PhysicalMarkerReplacement physicalMarkerReplacement(const Bonus & value, const BonusList & localExports)
{
	PhysicalMarkerReplacement result{value, false};
	for(const auto & existing : localExports)
	{
		if(!existing || existing->type != BonusType::PHYSICAL_AFFLICTION
			|| existing->source != value.source || existing->sid != value.sid) continue;
		result.replaceExisting = true;
		if(existing->duration == result.marker.duration && Bonus::NTurns(existing.get()))
			result.marker.turnsRemain = std::max(result.marker.turnsRemain, existing->turnsRemain);
	}
	return result;
}
CSelector physicalMarkerGroup(const Bonus & value)
{
	return [&value](const Bonus * bonus)
	{
		return bonus && bonus->type == BonusType::PHYSICAL_AFFLICTION
			&& bonus->source == value.source && bonus->sid == value.sid;
	};
}
GuardianPool guardianPool(const Bonus & value)
{
	if(value.val <= 0 || !Bonus::NTurns(&value) || value.turnsRemain <= 0)
		throw std::runtime_error("Invalid Guardian Spirit bonus pool or duration");
	return {value.val, value.turnsRemain};
}
bool selectsCurrentFormMarker(const battle::CUnitState & unit, const Bonus & value)
{
	if(!battle::isPolymorphMarker(&value) || !unit.hasBattleForm()) return false;
	const auto markers = unit.getBonuses(CSelector([&value](const Bonus * candidate)
	{
		return battle::isPolymorphMarker(candidate) && candidate->sid == value.sid && candidate->val == value.val;
	}));
	return markers && !markers->empty();
}
bool removesConfusion(const battle::Unit & unit, const Bonus & selected)
{
	const auto markers = unit.getBonuses(Selector::type()(BonusType::CONFUSION_PENDING));
	return std::ranges::any_of(*markers, [&](const auto & marker)
	{
		return newHorizonsConfusionControl::isPendingMarker(marker.get()) && battle::matchesStackEffectRemoval(selected, *marker);
	});
}
void finishConfusionRemoval(battle::CUnitState & unit, bool selected)
{
	if(selected && !unit.hasBonusOfType(BonusType::CONFUSION_PENDING)) unit.confusionState.clearPending();
}
void restoreGuardianAfterRemoval(battle::CUnitState & unit)
{
	const auto remaining = unit.getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT));
	if(!remaining || remaining->empty())
	{
		unit.guardianSpiritHitPoints = 0;
		unit.guardianSpiritRoundsRemaining = 0;
	}
	else
	{
		unit.guardianSpiritRoundsRemaining = 0;
		for(const auto & marker : *remaining)
			if(marker) unit.guardianSpiritRoundsRemaining = std::max<int32_t>(unit.guardianSpiritRoundsRemaining, marker->turnsRemain);
	}
}
}
