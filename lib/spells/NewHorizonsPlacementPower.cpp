/* NewHorizonsPlacementPower.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "NewHorizonsPlacementPower.h"
#include "ISpellMechanics.h"
#include "../json/JsonNode.h"
namespace newHorizonsMagic
{
int64_t verdantPrisonHitPoints(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower, bool warden)
{
	const int modifier = warden ? 125 : 100;
	const auto hundredths = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		3LL * std::max(0, power) * modifier, 1, coefficient, warcasting, empower);
	return (180LL * modifier + hundredths) / 100;
}
int64_t transfigureHitPoints(int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower,
	int32_t footprint, bool matterShaper)
{
	const auto term = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(2LL * power, 1, coefficient, warcasting, empower);
	const auto pool = 80 + term + 50LL * footprint;
	return matterShaper ? pool * 5 / 4 : pool;
}
int32_t earthquakeSectionCount(const JsonNode & profile, int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	const auto extra = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(std::max<int64_t>(0, power),
		profile["powerPerSection"].Integer(), coefficient, warcasting, empower);
	return static_cast<int32_t>(profile["baseSections"].Integer() + std::min<int64_t>(extra,
		profile["maxSections"].Integer() - profile["baseSections"].Integer()));
}
int64_t earthquakeRawDamage(const JsonNode & profile, int32_t power, int32_t coefficient, int32_t warcasting, int32_t empower)
{
	return profile["baseDamage"].Integer() + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		profile["powerNumerator"].Integer() * power, profile["powerDivisor"].Integer(), coefficient, warcasting, empower);
}
int64_t earthquakeStructuralDamage(int32_t base, bool geomancer)
{
	return geomancer ? static_cast<int64_t>(base) * 125 / 100 : base;
}
}
