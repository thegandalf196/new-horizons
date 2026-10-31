/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
#include "../../lib/battle/BattleUnitTurnReason.h"
class HypotheticBattle;
class StackWithBonuses;
namespace prismaticVanguardAI
{
void entry(HypotheticBattle & battle, StackWithBonuses & unit);
void begin(HypotheticBattle & battle, StackWithBonuses & unit, BattleUnitTurnReason reason);
void completeWait(StackWithBonuses & unit);
}
