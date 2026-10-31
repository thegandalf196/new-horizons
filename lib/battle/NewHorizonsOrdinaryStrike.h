/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include <vector>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace newHorizonsOrdinaryStrike
{
struct DLL_LINKAGE EffectPlan
{
	uint32_t target = 0;
	std::vector<Bonus> remove;
	std::vector<Bonus> add;
};
DLL_LINKAGE bool isStrikeEffect(const Bonus * bonus);
DLL_LINKAGE bool isInitiativeEffect(const Bonus * bonus);
DLL_LINKAGE bool mayChangeInitiative(const CBattleInfoCallback & battle);
/// Pure primary-hit plan. Callers own authoritative or copied bonus-tree mutation.
DLL_LINKAGE bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE EffectPlan plan(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, int64_t primaryDamage, int64_t primaryBodyHealthLoss = 0);
}
