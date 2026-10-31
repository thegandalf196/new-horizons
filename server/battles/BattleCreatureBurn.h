/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleUnitTurnReason.h"
class CGameHandler;
class CBattleInfoCallback;
class CStack;
namespace battleCreatureBurn
{
/// Consume the accepted pending burn before damage/rebirth can publish a new state.
bool onActivation(CGameHandler & game, const CBattleInfoCallback & battle, const CStack & target,
	BattleUnitTurnReason reason);
}
