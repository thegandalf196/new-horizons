/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <cstdint>
class IBonusBearer;
namespace creatureAbilityChance
{
struct Rounding
{
	int wholePercent = 0;
	int remainderBasisPoints = 0;
	bool operator==(const Rounding &) const = default;
};
/// No RNG. Native owns the optional fractional draw, then its unchanged biased stream.
DLL_LINKAGE Rounding rounding(const IBonusBearer & unit, int percentageChance);
}
