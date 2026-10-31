/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <memory>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace newHorizonsSepulchralGrip
{
DLL_LINKAGE bool isEffect(const Bonus * bonus);
DLL_LINKAGE std::shared_ptr<Bonus> primaryHit(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
}
