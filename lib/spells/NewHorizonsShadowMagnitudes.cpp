/*
 * NewHorizonsShadowMagnitudes.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsShadowMagnitudes.h"
#include "NewHorizonsMagic.h"
#include "ISpellMechanics.h"
#include "../mapObjects/CGHeroInstance.h"

#include <algorithm>
#include <stdexcept>

namespace newHorizonsMagic
{
int32_t lifeDrainHealingPercent(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(SHADOW_MAGIC_SKILL),
		"new-horizons:shadowMagic.bloodDrinker") ? 75 : 60;
}

int32_t vampirismHealBasisPoints(const int32_t rawSpellPower, const int32_t coefficientBasisPoints,
	const int32_t warcastingBonusPercent, const int32_t empowerBonusPercent, const bool nightFeeder)
{
	if(rawSpellPower < 0)
		throw std::invalid_argument("Invalid Vampirism raw Spell Power input");
	const int64_t scaledPowerBasisPoints = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		static_cast<int64_t>(rawSpellPower) * VAMPIRISM_SPELL_POWER_BASIS_POINTS_PER_POINT, 1,
		coefficientBasisPoints, warcastingBonusPercent, empowerBonusPercent);
	const int32_t baseHealing = VAMPIRISM_BASE_HEAL_BASIS_POINTS + static_cast<int32_t>(
		std::min<int64_t>(VAMPIRISM_MAX_BASE_HEAL_BASIS_POINTS - VAMPIRISM_BASE_HEAL_BASIS_POINTS,
			scaledPowerBasisPoints));
	return nightFeeder ? std::min(VAMPIRISM_MAX_HEAL_BASIS_POINTS,
		baseHealing + VAMPIRISM_NIGHT_FEEDER_BONUS_BASIS_POINTS) : baseHealing;
}

int32_t doomCripplingPenaltyPercent(const int32_t rawSpellPower, const int32_t coefficientBasisPoints,
	const int32_t warcastingBonusPercent, const int32_t empowerBonusPercent)
{
	const int64_t spellPowerTerm = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		static_cast<int64_t>(std::max<int32_t>(0, rawSpellPower)) * DOOM_SPELL_POWER_TERM_NUMERATOR,
		DOOM_SPELL_POWER_TERM_DIVISOR, coefficientBasisPoints, warcastingBonusPercent, empowerBonusPercent);
	return static_cast<int32_t>(std::min<int64_t>(DOOM_MAX_CRIPPLING_PERCENT,
		DOOM_BASE_CRIPPLING_PERCENT + spellPowerTerm));
}
}
