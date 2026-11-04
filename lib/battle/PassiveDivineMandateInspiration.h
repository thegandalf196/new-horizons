/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../spells/AutomaticHeroCast.h"
#include <vector>
class CBattleInfoCallback;
struct SetStackEffect;
namespace newHorizonsPassiveDivineMandate
{
/// Called after native resolution with actual successful effect recipients.
/// Fixed skill Speed is never transformed by spell potency or marked again.
DLL_LINKAGE SetStackEffect inspiringSpeedEffects(const CBattleInfoCallback & battle,
	BattleSide side, spells::AutomaticHeroCastReceipt receipt,
	const std::vector<uint32_t> & resolvedRecipientIds);
}
