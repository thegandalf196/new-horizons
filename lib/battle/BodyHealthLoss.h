/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cstdint>
namespace battle
{
/// Standard applied-damage receipt already includes clamping and eligible Guardian absorption.
/// Excludes only the captured prehit temporary pool; never infer from net post-Rebirth HP.
DLL_LINKAGE int64_t bodyHealthLoss(int64_t alreadyAppliedDamage, int64_t prehitTemporaryHealth);
}
