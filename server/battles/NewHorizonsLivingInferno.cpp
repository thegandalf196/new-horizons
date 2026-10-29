/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLivingInferno.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace newHorizonsLivingInferno
{
void publish(CGameHandler & handler, const CBattleInfoCallback & battle,
	const Trail & trail, BattleHex finalPosition)
{
	if(auto wall = obstacle(battle, trail, finalPosition))
	{
		BattleObstaclesChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		wall->toInfo(changed.change, BattleChanges::EOperation::ADD);
		handler.sendAndApply(changed);
	}
}
}
