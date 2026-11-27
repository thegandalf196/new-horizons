/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace newHorizonsPassiveSpecialties
{
DLL_LINKAGE double ordinarySpecialtyDamagePercent(const CBattleInfoCallback &, const BattleAttackInfo &);
}
