/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../battle/BattleSide.h"
#include "../battle/MetamagicDoubleCastState.h"
#include <vector>

class CBattleInfoCallback;
class ServerCallback;

namespace metamagicDoubleCast
{
/// Metamagic 6 completion effects, published by the authority after a double
/// cast's follow-up spell has resolved and been paid. `beforeFollowUp` is this
/// side's ledger captured before the follow-up's BattleSpellCast packet, so
/// it still names the first spell and the units it affected. Nothing happens
/// unless that packet completed a double cast under captured policy 7.
///
/// Every change is an ordinary game-state packet (SetStackEffect, BattleUnitsChanged,
/// SetMana): Afterglow, Cleansing Weave, Arcane Ward, Mana Siphon, Formula Reserve.
/// Crescendo and Spellstorm are derived terms and need no completion step.
DLL_LINKAGE void applyCompletionEffects(ServerCallback & server, const CBattleInfoCallback & battle,
	BattleSide side, const MetamagicDoubleCastState & beforeFollowUp, SpellID followUp,
	const std::vector<uint32_t> & followUpUnits);
}
