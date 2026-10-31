/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleVoluntaryMovementHistory.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsGnollHarriers.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/SetStackEffect.h"
namespace battleVoluntaryMovementHistory
{
void record(CGameHandler & game, const CBattleInfoCallback & battle, const battle::Unit & unit,
	bool voluntary, BattleHex before, BattleHex after)
{
	const auto marker = newHorizonsGnollHarriers::movement(battle, unit, voluntary, before, after);
	if(!marker)
		return;
	SetStackEffect effect;
	effect.battleID = battle.getBattle()->getBattleID();
	effect.toAdd.emplace_back(unit.unitId(), std::vector<Bonus>{*marker});
	game.sendAndApply(effect);
}
}
