/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleUnitTurnReason.h"
class CGameHandler;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace battleActivationEffects
{
void apply(CGameHandler * handler, const CBattleInfoCallback & battle, const battle::Unit * unit, BattleUnitTurnReason reason);
}
