/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
#include <optional>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class CUnitState; class Unit; }
namespace newHorizonsToxicSting
{
struct Plan
{
	uint32_t source = 0;
	uint32_t target = 0;
	int chancePercent = 0;
	int64_t baseDamage = 0;
};
DLL_LINKAGE bool livingTarget(const battle::Unit & target);
/// Prehit eligibility and already-prepared body loss; never infer from post-Rebirth HP.
/// Shared actual-attack eligibility/chance query; no synthetic damage or state.
DLL_LINKAGE int chancePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE std::optional<Plan> prepare(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, int64_t primaryBodyHealthLoss);
/// Reuses the sole physical Poison state/refresh owner; no roll or injury here.
DLL_LINKAGE bool apply(const Plan & plan, battle::CUnitState & target);
}
