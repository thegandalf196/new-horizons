/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
#include "BattleUnitTurnReason.h"
#include <optional>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace newHorizonsCreatureBurn
{
inline constexpr auto IDENTITY = "new-horizons:ordinary-creature-specialty:burn";
/// Exact marker validator. A claimed burn with malformed provenance throws.
DLL_LINKAGE bool isBurn(const Bonus * bonus);
DLL_LINKAGE Bonus marker(HeroTypeID hero, uint32_t source, int32_t rawDamage);
DLL_LINKAGE int32_t damageBudget(int64_t actualBodyHealthLoss, int percent);
DLL_LINKAGE bool eligible(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Actual body loss must exclude Guardian Spirit and temporary HP; capture at the hit seam.
DLL_LINKAGE newHorizonsOrdinaryStrike::EffectPlan plan(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, int64_t actualBodyHealthLoss);
struct DLL_LINKAGE Tick
{
	std::vector<Bonus> remove;
	uint32_t source = 0;
	int64_t damage = 0;
};
/// Shared pure lifecycle plan. Consume remove before injury, even when damage is immune/zero.
DLL_LINKAGE Tick activationTick(const CBattleInfoCallback & battle, const battle::Unit & target,
	BattleUnitTurnReason reason);
}
