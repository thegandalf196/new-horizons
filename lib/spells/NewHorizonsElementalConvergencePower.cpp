/*
 * NewHorizonsElementalConvergencePower.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsElementalConvergencePower.h"
#include "ISpellMechanics.h"

#include <algorithm>

namespace newHorizonsMagic
{
int64_t elementalConvergenceHitPoints(const int32_t rawSpellPower, const int32_t coefficientBasisPoints,
	const int32_t warcastingBonusPercent, const int32_t empowerSpellBonusPercent, const bool conjurer)
{
	const int64_t percent = conjurer ? 130 : 100;
	const int64_t spellPowerHundredths = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		5LL * std::max<int32_t>(0, rawSpellPower) * percent, 1,
		coefficientBasisPoints, warcastingBonusPercent, empowerSpellBonusPercent);
	return (250 * percent + spellPowerHundredths) / 100;
}
}
