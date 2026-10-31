/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleMeleeSpecialtyCompletion.h"
#include "BattlePrimaryStrikeAftermath.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsShieldAndSword.h"
namespace battleMeleeSpecialtyCompletion
{
void apply(CGameHandler & game, const CBattleInfoCallback & battle, uint32_t actorId, bool completedPrimaryMelee)
{
	if(const auto * actor = battle.battleGetUnitByID(actorId))
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(game, battle,
			newHorizonsShieldAndSword::completion(battle, *actor, completedPrimaryMelee));
}
}
