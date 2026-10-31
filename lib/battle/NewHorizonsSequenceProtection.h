/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace newHorizonsSequenceProtection
{
/// Attack-local values; no state, resource consumption, RNG or action generation.
DLL_LINKAGE int cyclopeanBreachPercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int runicEndurancePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
}
