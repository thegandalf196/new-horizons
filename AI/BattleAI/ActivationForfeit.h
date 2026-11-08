/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/CBattleInfoCallback.h"
namespace activationForfeit
{
inline bool pending(const CBattleInfoCallback & battle, const battle::Unit & unit, BattleUnitTurnReason reason)
{
	return newHorizonsFrozen::forfeitsNormalActivation(unit, reason)
		|| newHorizonsStun::forfeitsActivation(battle, unit, reason);
}
}
