/*
 * NewHorizonsStormOfDaggersDamage.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstdint>

namespace spells
{
/// The exact pre-recipient Storm of Daggers pool/share. Both modes round the
/// same full rational expression positive-half-up once: total=true is the
/// displayed pool; total=false is an equal per-target share, not rounded total/N.
/// Inputs are the captured formula/cast values; no target defenses or RNG.
/// Counts outside1..newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS return zero.
/// Invalid numeric domains return zero; checked arithmetic overflow throws.
DLL_LINKAGE int64_t stormOfDaggersRoundedDamage(int64_t base, int64_t powerCoefficient,
	int32_t effectPower, int32_t powerDivisor, int32_t spellPowerCoefficientBasisPoints,
	int32_t warcastingBonusPercent, int32_t empowerSpellBonusPercent,
	int32_t selectedTargetCount, bool total);
}
