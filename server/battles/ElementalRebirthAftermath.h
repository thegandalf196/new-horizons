/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsElementalRebirth.h"
class CGameHandler;
namespace elementalRebirthAftermath
{
struct Trigger
{
	BattleID battleID = BattleID::NONE;
	newHorizonsElementalRebirth::DeathSnapshot snapshot;
};
void resolve(CGameHandler & game, uint32_t unitId, const Trigger & trigger);
}
