/*
 * NewHorizonsRegenerationRate.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsMagic.h"
#include "ISpellMechanics.h"
namespace newHorizonsMagic
{
int32_t regenerationRateMillionths(const int32_t spellPower, const int schoolRankCoefficientPercent,
	const bool herbalist, const int warcastingBonusPercent)
{
	if(spellPower < 0 || schoolRankCoefficientPercent < 0 || schoolRankCoefficientPercent > 1000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000)
		throw std::invalid_argument("Invalid Regeneration rate inputs");

	// 0.15% x Spell Power becomes 15 x Spell Power millionths of the
	// fractional rate. Keep the exact school-rank product so successive hits
	// do not lose fractional marks to per-hit rounding.
	const int64_t spellPowerTerm = static_cast<int64_t>(15) * spellPower
		* schoolRankCoefficientPercent * (100 + warcastingBonusPercent) / 100;
	const int64_t base = REGENERATION_BASE_RATE_MILLIONTHS;
	const int64_t herbalistBonus = herbalist ? REGENERATION_HERBALIST_BONUS_MILLIONTHS : 0;
	return static_cast<int32_t>(std::min<int64_t>(REGENERATION_MAX_RATE_MILLIONTHS,
		base + spellPowerTerm + herbalistBonus));
}

int32_t regenerationRateMillionthsBasisPointsWithSpecialty(const int32_t spellPower, const int coefficientBasisPoints,
	const bool herbalist, const int warcastingBonusPercent, const int empowerSpellBonusPercent, const int specialtyPercent)
{
	if(spellPower < 0 || coefficientBasisPoints < 0 || coefficientBasisPoints > 100'000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000
		|| specialtyPercent < 0 || specialtyPercent > 200)
		throw std::invalid_argument("Invalid Regeneration basis-point inputs");

	// 10000 basis points is 100%. Keep the exact School × Spellcraft product,
	// Warcasting, Empower and the original specialty multiplier until the final rate floor.
	// Numerator * (100 + specialty), divisor 100 preserves fractional SP terms;
	// the bounded product remains below 1e15 even at maximum int32 Spell Power.
	const int64_t spellPowerTerm = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		1500LL * spellPower * (100 + specialtyPercent), 100, coefficientBasisPoints,
		warcastingBonusPercent, empowerSpellBonusPercent);
	const int64_t base = REGENERATION_BASE_RATE_MILLIONTHS;
	const int64_t herbalistBonus = herbalist ? REGENERATION_HERBALIST_BONUS_MILLIONTHS : 0;
	return static_cast<int32_t>(std::min<int64_t>(REGENERATION_MAX_RATE_MILLIONTHS,
		base + spellPowerTerm + herbalistBonus));
}

int32_t regenerationRateMillionthsBasisPoints(const int32_t spellPower, const int coefficientBasisPoints,
	const bool herbalist, const int warcastingBonusPercent, const int empowerSpellBonusPercent)
{
	return regenerationRateMillionthsBasisPointsWithSpecialty(spellPower, coefficientBasisPoints,
		herbalist, warcastingBonusPercent, empowerSpellBonusPercent, 0);
}
}
