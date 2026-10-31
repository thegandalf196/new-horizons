/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleHex.h"
class CGameHandler;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace battleVoluntaryMovementHistory
{
void record(CGameHandler & game, const CBattleInfoCallback & battle, const battle::Unit & unit,
	bool voluntary, BattleHex before, BattleHex after);
}
