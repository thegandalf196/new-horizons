/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MeleeSpecialtyProjection.h"
#include "CoordinatedMarkProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsShieldAndSword.h"
namespace meleeSpecialtyAI
{
bool completedPrimaryMelee(const AttackPossibility & preview)
{
	if(!preview.attackerState || preview.attack.shooting || preview.attack.retaliation
		|| !preview.effectPreview || preview.effectPreview->battleGetOwner(preview.attack.attacker)
			== preview.effectPreview->battleGetOwner(preview.attack.defender))
		return false;
	return std::ranges::any_of(preview.fortuneStrikes, [&](const auto & strike)
	{
		return strike.attackerId == preview.attackerState->unitId() && !strike.shooting
			&& !strike.retaliation && !strike.bracePreemptive && strike.cleaveDamagePercent == 0
			&& !strike.hits.empty();
	});
}
bool complete(HypotheticBattle & branch, uint32_t actorId, bool completed)
{
	const auto * actor = branch.battleGetUnitByID(actorId);
	if(!actor)
		return false;
	const auto effect = newHorizonsShieldAndSword::completion(branch, *actor, completed);
	if(effect.add.empty())
		return false;
	auto recipient = branch.getForUpdate(actorId);
	recipient->removeUnitBonus(effect.remove);
	recipient->addUnitBonus(effect.add);
	return true;
}
void complete(AttackPossibility & preview)
{
	coordinatedMarkAI::completeReturn(preview);
	if(preview.effectPreview && completedPrimaryMelee(preview)
		&& complete(*preview.effectPreview, preview.attackerState->unitId(), true))
		preview.attackerState = preview.effectPreview->getForUpdate(preview.attackerState->unitId());
}
}
