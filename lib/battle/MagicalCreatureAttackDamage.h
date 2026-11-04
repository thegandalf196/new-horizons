/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
class CBattleInfoCallback;
struct BattleAttackInfo;
struct DamageEstimation;
namespace battle
{
/// Ordinary magical attacks retain their native intrinsic mitigation and acquire
/// the specified passive Spellward source exactly once. Hero spell modifiers and
/// unrelated spell-only ANY bonuses are not part of this attack pipeline.
DLL_LINKAGE void applyMagicalAttackReduction(const CBattleInfoCallback &, const BattleAttackInfo &, DamageEstimation &);
}
