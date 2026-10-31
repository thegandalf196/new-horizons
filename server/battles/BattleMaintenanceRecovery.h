/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleUnitTurnReason.h"
class CGameHandler;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace battleMaintenanceRecovery
{
void onDefend(CGameHandler & handler, const CBattleInfoCallback & battle, const battle::Unit & unit, bool voluntary);
bool onActivation(CGameHandler & handler, const CBattleInfoCallback & battle, const battle::Unit & unit, BattleUnitTurnReason reason);
}
