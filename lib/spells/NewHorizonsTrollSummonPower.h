/*
 * NewHorizonsTrollSummonPower.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>

namespace newHorizonsMagic
{
/// Exact Summon Trolls HP budget, not a creature count. Beastcaller applies
/// 125% to both fixed and Spell-Power-derived parts; otherwise100%. Preserve
/// the scaler's hundredths floor followed by the final whole-HP floor.
/// Negative raw power clamps to zero; no legacy divisor or pool cap is added.
DLL_LINKAGE int64_t trollSummonHitPoints(int32_t rawSpellPower, int32_t coefficientBasisPoints,
	int32_t warcastingBonusPercent, int32_t empowerSpellBonusPercent, bool beastcaller);
}
