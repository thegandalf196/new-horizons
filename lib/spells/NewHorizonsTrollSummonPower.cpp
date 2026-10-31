/*
 * NewHorizonsTrollSummonPower.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsTrollSummonPower.h"
#include "ISpellMechanics.h"

#include <algorithm>

namespace newHorizonsMagic
{
int64_t trollSummonHitPoints(const int32_t rawSpellPower, const int32_t coefficientBasisPoints,
	const int32_t warcastingBonusPercent, const int32_t empowerSpellBonusPercent, const bool beastcaller)
{
	const int64_t modifierPercent = beastcaller ? 125 : 100;
	const int64_t spellPowerHundredths = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		5LL * std::max<int32_t>(0, rawSpellPower) * modifierPercent, 2,
		coefficientBasisPoints, warcastingBonusPercent, empowerSpellBonusPercent);
	return (100 * modifierPercent + spellPowerHundredths) / 100;
}
}
