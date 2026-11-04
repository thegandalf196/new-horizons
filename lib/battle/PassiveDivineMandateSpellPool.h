/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "PassiveDivineMandate.h"
#include "BattleAction.h"
#include "../spells/AutomaticHeroCast.h"
namespace newHorizonsPassiveDivineMandate
{
/// Bounded native legality/usefulness pass: known spells x friendly stack aims.
/// No speculative casts, cloned battle states, RNG or tactical action search.
DLL_LINKAGE std::vector<SpellChoice> buildSpellPool(const CBattleInfoCallback & battle,
	const CGHeroInstance & hero, spells::AutomaticHeroCastReceipt receipt,
	const std::vector<uint32_t> & orderRecipientUnitIds);
/// Stable native spell-specific choices shared by the pool and execution. Cure
/// selects the first eligible status; Purify selects sorted groups up to its cap.
DLL_LINKAGE BattleAction automaticSpellAction(const CBattleInfoCallback & battle,
	BattleSide side, SpellID spell, uint32_t aimUnitId);
}
