/*
 * HypotheticLifeDrainObservation.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"

std::shared_ptr<StackWithBonuses> HypotheticBattle::getForUpdateImpl(uint32_t id)
{
	auto iter = stackStates.find(id);

	if(iter == stackStates.end())
	{
		const battle::Unit * s = subject->battleGetUnitByID(id);

		auto ret = std::make_shared<StackWithBonuses>(this, s);
		stackStates[id] = ret;
		return ret;
	}
	else
	{
		return iter->second;
	}
}

void HypotheticBattle::invalidateLifeDrainFriendlyObservation(uint32_t id) noexcept
{
	if(lifeDrainFriendlyObservation && lifeDrainFriendlyObservation->id == id)
		lifeDrainFriendlyObservation->valid = false;
}

bool HypotheticBattle::beginLifeDrainFriendlyObservation(uint32_t id) noexcept
{
	if(lifeDrainFriendlyObservation)
	{
		lifeDrainFriendlyObservation->valid = false;
		return false;
	}
	try
	{
		const auto * unit = battleGetUnitByID(id);
		const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
		if(!unit || !state || !unit->alive() || unit->isGhost()
			|| state->hasBattleFormState() || state->isBattleFormRestorationPending())
			return false;
		const auto snapshot = state->acquireState();
		if(!snapshot)
			return false;
		lifeDrainFriendlyObservation = LifeDrainFriendlyObservation{id,
			snapshot->save(), state->getBattleFormViewRevision(), bloodrageDestroyedUnits.contains(id)};
		return true;
	}
	catch(...)
	{
		lifeDrainFriendlyObservation.reset();
		return false;
	}
}

bool HypotheticBattle::finishLifeDrainFriendlyObservation() noexcept
{
	if(!lifeDrainFriendlyObservation)
		return false;
	try
	{
		auto observation = std::move(*lifeDrainFriendlyObservation);
		lifeDrainFriendlyObservation.reset();
		const auto * unit = battleGetUnitByID(observation.id);
		const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
		if(!observation.valid || observation.updateInProgress || !unit || !state || !unit->alive() || unit->isGhost()
			|| state->hasBattleFormState() || state->isBattleFormRestorationPending()
			|| state->getBattleFormViewRevision() != observation.formViewRevision
			|| bloodrageDestroyedUnits.contains(observation.id) != observation.destroyedMembership)
			return false;
		const auto snapshot = state->acquireState();
		return snapshot && snapshot->save() == observation.state;
	}
	catch(...)
	{
		lifeDrainFriendlyObservation.reset();
		return false;
	}
}

void HypotheticBattle::cancelLifeDrainFriendlyObservation() noexcept
{
	lifeDrainFriendlyObservation.reset();
}
