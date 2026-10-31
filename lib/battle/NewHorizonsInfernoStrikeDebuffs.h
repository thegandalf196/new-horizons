/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <memory>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace newHorizonsInfernoStrikeDebuffs
{
DLL_LINKAGE bool isEffect(const Bonus * bonus);
/// Ordinary primary strike eligibility; damage/liveness and refresh belong to OrdinaryStrike::plan.
DLL_LINKAGE std::shared_ptr<Bonus> primaryHit(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
}
