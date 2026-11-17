/*
 * BracePrimaryState.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BracePrimaryState.h"
#include "ForecastNativeAdapter.h"
#include "HypotheticBattle.h"

namespace attackPreview
{
void finishBracePrimaryState(HypotheticBattle & branch, uint32_t actorId,
	uint32_t victimId, int64_t victimHealthBefore)
{
	auto actor = branch.getForUpdate(actorId);
	auto victim = branch.getForUpdate(victimId);
	// Native actor resources are captured before recipient UPDATE can remove
	// or unpropagate records. Brace is physical, but never a counter spend.
	if(!forecast::NativeAdapter::afterAttack(branch, *actor, false, false, true))
		actor->afterAttack(false, false, true);
	const auto expire = [&](const std::shared_ptr<StackWithBonuses> & unit, const CSelector & selector)
	{
		if(!forecast::NativeAdapter::expireEffectSelector(branch, unit->unitId(), selector))
			unit->removeUnitBonus(selector);
	};
	if(victim->getAvailableHealth() < victimHealthBefore) expire(victim, Bonus::UntilBeingAttacked);
	expire(actor, Bonus::UntilAttack);
	expire(actor, Bonus::UntilOwnAttack);
}
}
