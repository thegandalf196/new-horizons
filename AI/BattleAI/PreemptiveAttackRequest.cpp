/*
 * PreemptiveAttackRequest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "PreemptiveAttackRequest.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"

namespace attackPreview
{
std::optional<PreemptiveRequest> preparePreemptiveStage(PreemptiveStage stage,
	const CBattleInfoCallback & battle, const BattleAttackInfo & incoming,
	bool physicalCreatureAttack,
	battle::CUnitState & movedAttacker, battle::CUnitState & currentDefender,
	int attackIndex, int currentRound)
{
	if(!physicalCreatureAttack
		|| incoming.shooting || incoming.bracePreemptive || incoming.preemptiveDamagePercent > 0)
		return {};
	if(stage == PreemptiveStage::BATTLECRAFT)
	{
		if(!battle.battleMatchOwner(&movedAttacker, &currentDefender, false))
			return {};
		const int percent = newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
			battle.battleGetOwnerHero(&currentDefender), &currentDefender, currentRound);
		if(percent <= 0)
			return {};
		currentDefender.battlecraftPreemptiveStrikeRound = currentRound;
		BattleAttackInfo attack(&currentDefender, &movedAttacker, 0, false);
		attack.retaliation = true;
		attack.preemptiveDamagePercent = percent;
		attack.attackerPos = currentDefender.getPosition();
		attack.defenderPos = movedAttacker.getPosition();
		return PreemptiveRequest{attack};
	}
	if(!movedAttacker.alive() || !currentDefender.alive()
		|| movedAttacker.armorerLastStandEndedActivation)
		return {};
	const int distance = attackIndex == 0 ? incoming.chargeDistance : 0;
	if(!battle.battleCanTriggerHeroOrderBrace(&movedAttacker, &currentDefender,
		distance, false, incoming.retaliation))
		return {};
	// Brace is a reactive strike but neither a damage-retaliation multiplier nor
	// ordinary counter consumption. The shared damage owner reads its saved Order.
	BattleAttackInfo attack(&currentDefender, &movedAttacker, 0, false);
	attack.attackerPos = currentDefender.getPosition();
	attack.defenderPos = movedAttacker.getPosition();
	attack.bracePreemptive = true;
	return PreemptiveRequest{attack, true, true};
}
}
