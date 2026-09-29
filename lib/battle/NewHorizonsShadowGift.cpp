/*
 * NewHorizonsShadowGift.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "NewHorizonsShadowGift.h"

#include "../spells/ISpellMechanics.h"

namespace newHorizonsShadowGift
{
bool isValidSacrificePercent(const int32_t choicePercent)
{
	return choicePercent == 10 || choicePercent == 20 || choicePercent == 30;
}

int32_t getSacrificeCostBasisPoints(const int32_t choicePercent, const bool darkGift)
{
	if(!isValidSacrificePercent(choicePercent))
		throw std::invalid_argument("Invalid Shadow Gift sacrifice choice");

	const int32_t selectedBasisPoints = choicePercent * 100;
	return darkGift ? selectedBasisPoints * DARK_GIFT_COST_PERCENT / 100 : selectedBasisPoints;
}

int32_t getDamageBonusBasisPoints(const int32_t choicePercent, const int32_t spellPower,
	const int32_t schoolCoefficientBasisPoints, const int32_t warcastingBonusPercent,
	const int32_t empowerBonusPercent)
{
	if(!isValidSacrificePercent(choicePercent) || spellPower < 0)
		throw std::invalid_argument("Invalid Shadow Gift damage inputs");

	const int64_t scaledSpellPowerBasisPoints = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		static_cast<int64_t>(spellPower) * 50, 1, schoolCoefficientBasisPoints,
		warcastingBonusPercent, empowerBonusPercent);
	const int64_t damageBonusBasisPoints =
		(static_cast<int64_t>(12'500) + scaledSpellPowerBasisPoints) * choicePercent / 100;
	if(damageBonusBasisPoints > std::numeric_limits<int32_t>::max())
		throw std::overflow_error("Shadow Gift damage bonus overflows");
	return static_cast<int32_t>(damageBonusBasisPoints);
}

int64_t getSacrificeHealthAmount(const int64_t aggregateHealth, const int32_t costBasisPoints)
{
	if(aggregateHealth < 0 || costBasisPoints < 0 || costBasisPoints > BASIS_POINTS_PER_WHOLE)
		throw std::invalid_argument("Invalid Shadow Gift health-cost inputs");
	if(aggregateHealth <= 1 || costBasisPoints == 0)
		return 0;

	const int64_t roundedDown = aggregateHealth / BASIS_POINTS_PER_WHOLE * costBasisPoints
		+ aggregateHealth % BASIS_POINTS_PER_WHOLE * costBasisPoints / BASIS_POINTS_PER_WHOLE;
	return std::min(aggregateHealth - 1, std::max<int64_t>(1, roundedDown));
}
}
