/*
 * NewHorizonsElementalConvergencePower.h, part of VCMI engine
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
/// Exact current Elemental Convergence HP budget. Conjurer applies130% to the
/// fixed250HP and the full Spell-Power term before the hundredths/whole-HP floors.
/// No legacy power divisor, HP cap, terrain selection or creature-count estimate.
DLL_LINKAGE int64_t elementalConvergenceHitPoints(int32_t rawSpellPower, int32_t coefficientBasisPoints,
	int32_t warcastingBonusPercent, int32_t empowerSpellBonusPercent, bool conjurer);
}
