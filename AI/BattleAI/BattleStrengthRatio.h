/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

#include "../../Global.h"
#include "../../lib/battle/BattleSide.h"

class CBattleInfoCallback;

namespace battleAI
{
/// Existing roster/creature eligibility and float scoring boundary, with wide
/// intermediate arithmetic for unrestricted stack counts. Shared by Full/Fast.
DLL_LINKAGE float strengthRatio(const CBattleInfoCallback & battle, BattleSide side);
}
