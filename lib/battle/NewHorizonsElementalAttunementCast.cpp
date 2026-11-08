/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsElementalAttunement.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "../networkPacks/SetElementalAttunementState.h"
#include <vcmi/ServerCallback.h>

void newHorizonsElementalAttunement::publishCompletedHeroCast(ServerCallback & server,
	const CBattleInfoCallback & battle, BattleSide side, SpellID spell)
{
	if(!battle.getBattle()) return;
	const uint8_t before = battle.getBattle()->getElementalAttunementMask(side);
	const uint8_t after = completedHeroCastMask(battle, side, spell, before);
	if(before == after) return;
	SetElementalAttunementState update;
	update.battleID = battle.getBattle()->getBattleID();
	update.side = side;
	update.spellID = spell;
	update.beforeMask = before;
	update.afterMask = after;
	server.apply(update);
}
