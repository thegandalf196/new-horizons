/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include "BattleUnitTurnReason.h"
#include <vector>
namespace battle { class Unit; }
class CBattleInfoCallback;
namespace newHorizonsStun
{
DLL_LINKAGE bool isMarker(const Bonus *);
DLL_LINKAGE bool isStunned(const battle::Unit &);
DLL_LINKAGE bool forfeitsActivation(const CBattleInfoCallback &, const battle::Unit &, BattleUnitTurnReason);
DLL_LINKAGE Bonus marker();
DLL_LINKAGE std::vector<Bonus> removalPlan(const battle::Unit &);
}
