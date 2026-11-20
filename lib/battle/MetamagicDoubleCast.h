/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "BattleSide.h"
#include "HeroActionAllowanceState.h"
#include "MetamagicDoubleCastState.h"
#include <optional>
#include <string>
#include <vector>

class BattleAction;
class BattleInfo;
class CBattleInfoCallback;
class CGHeroInstance;

/// Metamagic 6 (Double Cast) offer lifecycle. Every mutation runs in game-state
/// visitors (or BattleInfo round/activation transitions they drive), so client
/// and authority stay in step; the authority only validates before StartAction.
///
/// Offer: an accepted hero spell paid by a flexible Hero allowance, under captured
/// policy 7, with Metamagic rank > 0 and a use remaining, grants one typed Spell
/// allowance (GrantSource::METAMAGIC_DOUBLE_CAST) that expires this round.
/// Consume: the spell paid by that grant spends one use and completes the double
/// cast; it never opens another offer. Forfeit: any other accepted action, a new
/// creature activation, or the round end removes the grant at no cost.
namespace metamagicDoubleCast
{
using Ledger = HeroActionAllowanceState;

/// Open-offer signal visible through every battle callback.
DLL_LINKAGE bool offerGranted(const Ledger & ledger);
/// Combat maximum under the hero's own captured rules; 0 unless policy 7 and rank > 0.
DLL_LINKAGE int maximumUses(const CGHeroInstance * hero);
DLL_LINKAGE int remainingUses(const CGHeroInstance * hero, const MetamagicDoubleCastState & state);

/// Staged accepted-cast transition. `ledger` is the caller's staged copy that
/// already spent `receipt`; the returned state is committed together with it.
DLL_LINKAGE MetamagicDoubleCastState acceptHeroSpell(const BattleInfo & battle, BattleSide side,
	const CGHeroInstance * hero, SpellID spell, const std::vector<uint32_t> & affectedUnits,
	Ledger & ledger, const Ledger::Receipt & receipt);
/// Any accepted action other than this side's own hero spell forfeits its offer.
DLL_LINKAGE void forfeitOnStartAction(BattleInfo & battle, const BattleAction & action);
/// A new creature activation ends the "immediately after" window.
DLL_LINKAGE void expireOnActivation(BattleInfo & battle);
/// After the allowance ledgers advanced to `round`: expire offers, apply Arcane Cycle.
DLL_LINKAGE void startRound(BattleInfo & battle, int32_t round);

/// Authority pre-StartAction admission. Returns a refusal reason, or nothing.
DLL_LINKAGE std::optional<std::string> actionRefusal(const CBattleInfoCallback & battle, const BattleAction & action);
}
