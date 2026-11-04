/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>
struct Bonus;
enum class BattleSide : int8_t;
namespace battle { class Unit; }
namespace newHorizonsPassiveDivineMandate
{
/// Canonical capability discriminator; metadata never acts as a stat bonus.
/// Recognizes the family even if corrupt payload fields need rejecting below.
DLL_LINKAGE bool isVanguardRiderEntitlement(const Bonus & bonus);
DLL_LINKAGE void validateVanguardRiderEntitlement(const Bonus & bonus);
/// Captured fixed rider flags, only with real native Vanguard benefits and the
/// same effective owning side. Native bonus expiry/pausing owns their lifetime.
DLL_LINKAGE uint16_t vanguardRiderPerks(const battle::Unit * unit, BattleSide currentSide);
}
