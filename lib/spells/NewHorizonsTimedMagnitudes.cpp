/* NewHorizonsTimedMagnitudes.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "NewHorizonsTimedMagnitudes.h"
#include "ISpellMechanics.h"
namespace newHorizonsMagic
{
FrailtyAccumulation frailtyAccumulation(int32_t baseDefense, int32_t previousTotalBasisPoints,
	int64_t perCastBasisPoints)
{
	if(baseDefense < 0 || previousTotalBasisPoints < 0 || perCastBasisPoints < 0)
		throw std::invalid_argument("Invalid Frailty accumulation inputs");
	const int32_t total = std::min(6000, std::min(6000, previousTotalBasisPoints)
		+ static_cast<int32_t>(std::min<int64_t>(6000, perCastBasisPoints)));
	return {total, static_cast<int32_t>(static_cast<int64_t>(baseDefense) * total / 10000)};
}

int32_t frailtyDefenseLoss(int32_t power, int32_t coefficient,
	int32_t empower, int32_t specialty, bool withering)
{
	const auto term = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		5LL * std::max(0, power), 1, coefficient, empower, specialty);
	return static_cast<int32_t>(std::min<int64_t>(2000, 1000 + term)) + (withering ? 500 : 0);
}
int64_t hydraCapacityMillionths(int32_t power, int32_t coefficient,
	int32_t empower, int32_t specialty)
{
	return std::min<int64_t>(50'000'000, 25'000'000 + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		150'000LL * std::max(power, 0), 1, coefficient, empower, specialty));
}
int32_t entangleBaseDuration(int32_t power, int32_t coefficient,
	int32_t empower, bool rootcaller)
{
	const auto term = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		std::max(0, power), 100, coefficient, empower);
	const auto base = std::min<int64_t>(2, 1 + term);
	return static_cast<int32_t>(rootcaller ? std::min<int64_t>(3, base + 1) : base);
}
int32_t misfortuneBaseDuration(int32_t power, int32_t coefficient, int32_t empower)
{
	return static_cast<int32_t>(std::min<int64_t>(4, 2 + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		std::max(0, power), 80, coefficient, empower)));
}
int32_t misfortuneChanceMultiplier(int32_t power, int32_t coefficient,
	int32_t empower, bool weaver)
{
	const auto term = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		25LL * std::max(0, power), 1, coefficient, empower);
	return static_cast<int32_t>(std::max<int64_t>(2500, 7500 - term - (weaver ? 1000 : 0)));
}
int32_t shieldOfChaosReduction(int32_t power, int32_t coefficient,
	int32_t empower, bool paradox)
{
	const auto term = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		15LL * std::max(0, power), 1, coefficient, empower);
	return static_cast<int32_t>(std::min<int64_t>(8000, 5000 + term)) + (paradox ? 1000 : 0);
}
int64_t divineRetributionCap(int32_t power, int32_t coefficient,
	int32_t empower, int32_t crown)
{
	if(crown < 0 || crown > 1000)
		throw std::invalid_argument("Invalid Divine Retribution recipient multiplier");
	return 25 + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		125LL * std::max(0, power) * (100 + crown), 10000, coefficient, empower);
}
int64_t divineRetributionPayout(int64_t actualHpDamage, int64_t rawCap, int32_t retributionistPercent,
	int32_t payoutRatioBasisPoints)
{
	if(payoutRatioBasisPoints < 3000 || payoutRatioBasisPoints > 6900)
		throw std::invalid_argument("Invalid captured Divine Retribution payout ratio");
	const int64_t ratioDamage = actualHpDamage / 10000 * payoutRatioBasisPoints
		+ actualHpDamage % 10000 * payoutRatioBasisPoints / 10000;
	const int64_t cappedDamage = std::min(ratioDamage, rawCap);
	return cappedDamage * retributionistPercent / 100;
}
}
