/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleMaintenanceRecovery.h"
#include "../CGameHandler.h"
#include "../../lib/battle/NewHorizonsMaintenanceSpecialties.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace battleMaintenanceRecovery
{
namespace
{
bool publish(CGameHandler & handler, const CBattleInfoCallback & battle, const battle::Unit & unit, int healing)
{
	if(healing <= 0)
		return false;
	BattleTriggerEffect effect;
	effect.battleID = battle.getBattle()->getBattleID();
	effect.stackID = unit.unitId();
	effect.effect = BonusType::HP_REGENERATION;
	effect.val = healing;
	handler.sendAndApply(effect);
	return true;
}
}
void onDefend(CGameHandler & handler, const CBattleInfoCallback & battle, const battle::Unit & unit, bool voluntary)
{
	if(voluntary)
		publish(handler, battle, unit, newHorizonsMaintenanceSpecialties::woundHealing(
			battle, unit, newHorizonsMaintenanceSpecialties::Trigger::VOLUNTARY_DEFEND));
}
bool onActivation(CGameHandler & handler, const CBattleInfoCallback & battle, const battle::Unit & unit, BattleUnitTurnReason reason)
{
	return publish(handler, battle, unit, newHorizonsMaintenanceSpecialties::activationHealing(battle, unit, reason));
}
}
