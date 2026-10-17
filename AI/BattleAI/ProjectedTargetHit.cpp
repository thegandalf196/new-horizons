/*
 * ProjectedTargetHit.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHit.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsArmorer.h"

namespace projectedTargetHit
{
Prepared prepare(HypotheticBattle & battle, const battle::Unit & target,
	int64_t requestedDamage, Context context)
{
	auto appliedDamage = std::max<int64_t>(0, requestedDamage);
	const auto standSide = battle.playerToSide(battle.battleGetOwner(&target));
	const bool validStandSide = standSide == BattleSide::ATTACKER || standSide == BattleSide::DEFENDER;
	const bool eligibleForLastStand = context.eligibleForLastStand && validStandSide
		&& newHorizonsArmorer::canTriggerLastStand(battle.battleGetOwnerHero(&target), &target);
	const auto lastStand = validStandSide
		? newHorizonsArmorer::resolveLastStandDamage(appliedDamage,
			target.getGuardianSpiritHitPoints(), target.getGuardianSpiritRoundsRemaining(),
			target.getAvailableHealth(), eligibleForLastStand,
			battle.armorerLastStandUsed(standSide))
		: newHorizonsArmorer::ArmorerLastStandDamageResult{appliedDamage, false};
	appliedDamage = lastStand.damageToApply;
	if(lastStand.triggered)
		battle.consumeArmorerLastStand(standSide);
	return {context, appliedDamage, lastStand.triggered};
}

bool finishAfterDefiant(HypotheticBattle & battle, battle::CUnitState & target,
	const Prepared & prepared)
{
	if(prepared.lastStandTriggered)
	{
		battle.applyArmorerLastStandDefend(prepared.context.targetId);
		const auto activeStackId = battle.getActiveStackID();
		if(prepared.context.reactiveForLastStand && activeStackId >= 0
			&& prepared.context.targetId == static_cast<uint32_t>(activeStackId))
		{
			target.armorerLastStandEndedActivation = true;
			return true;
		}
	}
	return false;
}
}
