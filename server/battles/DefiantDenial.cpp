/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "DefiantDenial.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace
{
bool publish(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const battle::Unit & attacker, const battle::Unit & target, newHorizonsArmorer::DefiantDenialCause cause,
	const newHorizonsAerialSupremacy::Excursion * aerial = nullptr)
{
	const auto side = battle.playerToSide(battle.battleGetActionController(&target));
	SetArmorerDefiantState update;
	update.battleID = battle.getBattle()->getBattleID();
	update.side = side;
	update.attackerId = attacker.unitId();
	update.targetId = target.unitId();
	update.cause = cause;
	if(aerial) update.aerialExcursion = *aerial;
	update.state = battle.getBattle()->getArmorerDefiantState(side);
	if(!update.state.consumeAt(battle.getBattle()->getRound()))
		return false;
	gameHandler.sendAndApply(update);
	return true;
}
}
namespace battleDefiant
{
bool consume(CGameHandler & gameHandler, const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, newHorizonsArmorer::DefiantDenialCause cause)
{
	if(!battle.battleCanUseDefiant(attack, cause))
		return false;
	return publish(gameHandler, battle, *attack.attacker, *attack.defender, cause);
}
bool consumeGaze(CGameHandler & handler, const CBattleInfoCallback & battle,
	const battle::Unit & opponent, const battle::Unit & retaliator, bool ranged,
	const newHorizonsAerialSupremacy::Excursion * aerial)
{
	return battle.battleCanUseGazeDefiant(&opponent, &retaliator, ranged, aerial)
		&& publish(handler, battle, opponent, retaliator, newHorizonsArmorer::DefiantDenialCause::PARALYZING_GAZE, aerial);
}
}
