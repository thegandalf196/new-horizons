/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
#include "BattleUnitTurnReason.h"
#include <optional>
class CBattleInfoCallback;
struct BattleAttackInfo;
namespace battle { class Unit; class ActivationContext; }
namespace newHorizonsMagicalAbilityDamage { class FireDamageContext; }
namespace newHorizonsCreatureBurn
{
inline constexpr auto IDENTITY = "new-horizons:ordinary-creature-specialty:burn";
/// Public registered identities copied within the caller's actual authorized library interval.
/// Value-only metadata; this is not source/visibility/lifetime admission.
struct IdentityFacts
{
	HeroTypeID rashka;
	HeroTypeID ignissa;
};
DLL_LINKAGE IdentityFacts captureIdentityFacts();
/// Exact marker validator. A claimed burn with malformed provenance throws.
DLL_LINKAGE bool isBurn(const Bonus * bonus);
DLL_LINKAGE bool isBurnCaptured(const Bonus * bonus, const IdentityFacts & identities);
DLL_LINKAGE Bonus marker(HeroTypeID hero, uint32_t source, int32_t rawDamage);
DLL_LINKAGE int32_t damageBudget(int64_t actualBodyHealthLoss, int percent);
/// Applied strike loss already excludes Guardian Spirit; remove the captured prehit temporary pool.
DLL_LINKAGE int64_t bodyHealthLoss(int64_t appliedDamage, int64_t temporaryHealthBefore);
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
DLL_LINKAGE Tick activationTickCaptured(const CBattleInfoCallback & battle, const battle::Unit & target,
	BattleUnitTurnReason reason, const IdentityFacts & identities);
/// Current owned context route; no guessed activation flag or native fallback.
DLL_LINKAGE Tick activationTickCaptured(const battle::Unit & target, BattleUnitTurnReason reason,
	const IdentityFacts & identities, const battle::ActivationContext & activation,
	const newHorizonsMagicalAbilityDamage::FireDamageContext & fire);
}
