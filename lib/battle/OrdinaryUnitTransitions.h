/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include <optional>
namespace battle
{
/// Explicit ordinary flags only. Form/summon/status/reaction work remains with
/// its native lifecycle owner and must be admitted separately by a forecast.
DLL_LINKAGE void resetOrdinaryRoundFlags(bool & defending, bool & waiting,
	bool & waitedThisTurn, bool & movedThisRound, bool & hadMorale,
	bool & waitBonusUsed, bool & waitMasteryDoubled) noexcept;
/// The same finite preflight used by CAmmo::canUse. Unlimited counters retain
/// their observed availability; no request can overflow or underflow a counter.
DLL_LINKAGE void finishOrdinaryAttackFlags(bool waitedThisTurn, bool physical,
	bool & battlecraftWaitBonusUsed, bool & battlecraftWaitMasteryDoubled) noexcept;
DLL_LINKAGE std::optional<int32_t> consumeCounterValue(int32_t available, bool limited, int32_t amount) noexcept;
}
