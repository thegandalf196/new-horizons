/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsAttackControllers.h"
class CGameHandler;
namespace battleSpecialtyAttackControllers
{
/// False means this exact native battle retired during a cast; callers stop
/// before reading any earlier unit/callback references.
bool apply(CGameHandler &, const CBattleInfoCallback &, const newHorizonsAttackControllers::Strike &);
}
