/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsBattlecraft.h"
class CGameHandler;
class CBattleInfoCallback;
class CStack;
namespace battlecraftMasteryAward
{
void apply(CGameHandler & handler, const CBattleInfoCallback & battle, const CStack * unit, BattlecraftMasteryAction action);
}
