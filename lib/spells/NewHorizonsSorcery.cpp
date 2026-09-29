/*
 * NewHorizonsSorcery.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsSorcery.h"

#include <algorithm>
#include <stdexcept>

namespace newHorizonsSorcery
{
namespace
{
void validateSpellPower(int32_t spellPower)
{
	if(spellPower < 0)
		throw std::invalid_argument("Sorcery spell power cannot be negative");
}
}

int32_t arcaneBreachMarkBasisPoints(int32_t spellPower)
{
	validateSpellPower(spellPower);
	const int64_t uncapped = ARCANE_BREACH_BASE_BASIS_POINTS
		+ static_cast<int64_t>(spellPower) * ARCANE_BREACH_POWER_BASIS_POINTS;
	return static_cast<int32_t>(std::min<int64_t>(ARCANE_BREACH_CAP_BASIS_POINTS, uncapped));
}

int32_t phantomArmyIntegrityBasisPoints(int32_t spellPower, bool illusionist)
{
	validateSpellPower(spellPower);

	const int64_t uncapped = static_cast<int64_t>(PHANTOM_ARMY_BASE_INTEGRITY_PERCENT) * 100
		+ static_cast<int64_t>(spellPower) * 15;
	const int32_t base = static_cast<int32_t>(std::min<int64_t>(
		PHANTOM_ARMY_INTEGRITY_CAP_PERCENT * 100,
		uncapped));
	if(!illusionist)
		return base;

	// Illusionist is a multiplicative 25% integrity increase applied after the
	// canonical base cap.  Keeping the result in basis points preserves the
	// 43.75% value at Spell Power 100 instead of silently rounding it to 43%.
	return base * (100 + PHANTOM_ARMY_ILLUSIONIST_BONUS_PERCENT) / 100;
}

int64_t phantomArmyIntegrity(int64_t sourceCurrentHealth, int32_t spellPower, bool illusionist)
{
	if(sourceCurrentHealth < 0)
		throw std::invalid_argument("Phantom Army source health cannot be negative");

	const int32_t basisPoints = phantomArmyIntegrityBasisPoints(spellPower, false);
	const int64_t numerator = basisPoints * (illusionist ? 100 + PHANTOM_ARMY_ILLUSIONIST_BONUS_PERCENT : 100);
	constexpr int64_t denominator = 1000000;
	// Divide before multiplying to avoid an unnecessary wide intermediate while
	// retaining exact integer floor semantics for ordinary game-sized stacks.
	const int64_t whole = sourceCurrentHealth / denominator;
	const int64_t remainder = sourceCurrentHealth % denominator;
	return whole * numerator + remainder * numerator / denominator;
}

int phantomArmyDamageTakenPercent(bool magical)
{
	return magical ? PHANTOM_ARMY_MAGICAL_DAMAGE_TAKEN_PERCENT : PHANTOM_ARMY_PHYSICAL_DAMAGE_TAKEN_PERCENT;
}

int timeStopRadius(int32_t spellPower, bool chronomancer)
{
	return timeStopRadius(spellPower, chronomancer, 100);
}

int timeStopRadius(int32_t spellPower, bool chronomancer, int32_t coefficientPercent)
{
	validateSpellPower(spellPower);
	if(coefficientPercent < 0 || coefficientPercent > 1000)
		throw std::invalid_argument("Invalid Time Stop Spell Power coefficient");
	const int maximumRadius = TIME_STOP_BASE_MAX_RADIUS
		+ (chronomancer ? TIME_STOP_CHRONOMANCER_RADIUS_BONUS : 0);
	const int64_t scaledPowerTerm = static_cast<int64_t>(spellPower) * coefficientPercent
		/ (TIME_STOP_POWER_PER_EXTRA_RADIUS * 100);
	return std::min(maximumRadius, TIME_STOP_BASE_RADIUS + static_cast<int>(scaledPowerTerm));
}

int spellLockDuration(int32_t spellPower, bool spellbinder, int32_t coefficientPercent,
	int32_t warcastingBonusPercent)
{
	if(coefficientPercent < 0 || coefficientPercent > 1000)
		throw std::invalid_argument("Invalid Spell Lock Spell Power coefficient inputs");
	return spellLockDurationBasisPoints(spellPower, spellbinder,
		coefficientPercent * 100, warcastingBonusPercent);
}

int spellLockDurationBasisPoints(int32_t spellPower, bool spellbinder,
	int32_t coefficientBasisPoints, int32_t warcastingBonusPercent)
{
	validateSpellPower(spellPower);
	if(coefficientBasisPoints < 0 || coefficientBasisPoints > 100000 || warcastingBonusPercent < 0)
		throw std::invalid_argument("Invalid Spell Lock Spell Power coefficient inputs");

	// Match Mechanics::scaleSpellPowerComponentWithCoefficient exactly while
	// avoiding a product of Spell Power, the rank coefficient, and Warcasting.
	// int32 Spell Power times the bounded coefficient fits in int64; quotient/
	// remainder scaling keeps the additional Warcasting multiplier overflow-safe.
	constexpr int64_t denominator = static_cast<int64_t>(SPELL_LOCK_POWER_PER_EXTRA_ROUND) * 10000 * 100;
	const int64_t scaledNumerator = static_cast<int64_t>(spellPower) * coefficientBasisPoints;
	const int64_t multiplier = 100LL + warcastingBonusPercent;
	const int64_t whole = scaledNumerator / denominator;
	const int64_t remainder = scaledNumerator % denominator;
	const int64_t scaledSpellPower = whole * multiplier + remainder * multiplier / denominator;
	const int baseDuration = static_cast<int>(std::min<int64_t>(
		SPELL_LOCK_BASE_DURATION_CAP,
		1 + scaledSpellPower));
	if(!spellbinder)
		return baseDuration;

	return std::min(
		SPELL_LOCK_SPELLBINDER_DURATION_CAP,
		baseDuration + SPELL_LOCK_SPELLBINDER_DURATION_BONUS);
}

SpellLockPolicy spellLockPolicy(bool friendlyTarget)
{
	SpellLockPolicy policy;
	if(friendlyTarget)
	{
		policy.removeHostile = true;
		policy.preserveBeneficial = true;
	}
	else
	{
		policy.removeBeneficial = true;
		policy.preserveHostile = true;
	}
	return policy;
}
}
