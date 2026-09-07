/*
 * NewHorizonsPrimaryScale.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../GameConstants.h"
#include <cstdint>

namespace newHorizonsHeroes
{
/// Pure arithmetic for future saved-scale integration, not a change to casters.
/// Scale only the power-dependent term; fixed mastery/effect values retain their
/// magnitude. Integer effects truncate the scaled term after multiplication.
/// All inputs are nonnegative; the divisor must be positive and is supplied by
/// rules, not silently chosen here. Wide intermediates avoid rating overflow.
DLL_LINKAGE int64_t scaledPowerEffect(int fixedValue, int perPower, int powerRating, int divisor);

/// Knowledge directly supplies base mana in the redesign. The percentage is the
/// final skill/artifact multiplier (100 means unchanged), not another rolled stat.
/// This helper is not yet wired to the hero or frontend; legacy mana is untouched.
DLL_LINKAGE int64_t manaFromKnowledge(int knowledge, int multiplierPercent);
}
