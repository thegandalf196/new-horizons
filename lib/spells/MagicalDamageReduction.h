/*
 * MagicalDamageReduction.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../Global.h"

#include <cstdint>
#include <vector>

namespace spells
{
struct DLL_LINKAGE MagicalDamageReductionResult
{
	int64_t damageWithoutPenetration = 0;
	int64_t damageWithPenetration = 0;

	bool operator==(const MagicalDamageReductionResult &) const = default;
};

/// Applies independent magical-damage reductions and optional relative
/// penetration to raw damage. Independent reductions multiply, aggregate MDR
/// is capped at 95%, and penetration reduces that capped aggregate rather than
/// subtracting percentage points. Both returned damage values are floored once
/// after the complete product is evaluated.
DLL_LINKAGE MagicalDamageReductionResult calculateMagicalDamageReduction(
	int64_t rawDamage, const std::vector<int> & independentReductionsPercent, int penetrationPercent);
}
