/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BarrageProbability.h"
#include "../../lib/battle/IBattleInfoCallback.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace barrageProbability
{
namespace
{
using Integer = boost::multiprecision::cpp_int;
Integer choose(Integer top, unsigned count)
{
	if(top < count) return 0;
	Integer result = 1;
	for(unsigned index = 1; index <= count; ++index)
	{
		result *= top - index + 1;
		result /= index;
	}
	return result;
}
Integer atMost(unsigned samples, const Integer & width, const Integer & threshold, const Integer & outcomes)
{
	if(threshold < 0) return 0;
	if(threshold >= width * samples) return outcomes;
	Integer result = 0, selections = 1;
	const Integer sides = width + 1;
	for(unsigned excluded = 0; excluded <= samples; ++excluded)
	{
		const Integer remaining = threshold - sides * excluded;
		if(remaining < 0) break;
		const Integer term = selections * choose(remaining + samples, samples);
		if(excluded % 2) result -= term;
		else result += term;
		selections *= samples - excluded;
		selections /= excluded + 1;
	}
	if(result < 0 || result > outcomes) throw std::logic_error("Invalid exact damage-tail count");
	return result;
}
}
long double primaryCasualtyAndSurvival(const DamageRange & range, int32_t count, int shots, const TargetWindow & target)
{
	if(count <= 0 || shots <= 0 || target.totalHealth == 0 || target.firstCreatureHP == 0) return 0;
	if(range.min < 0 || range.max < range.min || target.firstCreatureHP < 0 || target.totalHealth < 0
		|| target.temporaryHitPoints < 0 || target.arcaneShield < 0 || target.guardianSpirit < 0)
		throw std::invalid_argument("Invalid nominal barrage damage or health interval");
	const Integer absorption = Integer(target.arcaneShield) + target.guardianSpirit;
	const Integer first = Integer(target.firstCreatureHP) + target.temporaryHitPoints + absorption;
	const Integer total = Integer(target.totalHealth) + absorption;
	if(first >= total) return 0; // A casualty would destroy the only remaining creature.
	const Integer minimum = Integer(range.min) * shots;
	const Integer maximum = Integer(range.max) * shots;
	if(maximum < first || minimum >= total) return 0;
	if(minimum >= first && maximum < total) return 1;
	if(range.min == range.max) return 0;
	// Extra grants do not expand this bounded probability calculation. Ignoring
	// their kill opportunity is conservative only if they cannot destroy the target.
	if(shots > 2 && maximum >= total) return 0;
	const unsigned modeledShots = static_cast<unsigned>(std::min(shots, 2));
	const unsigned perShot = static_cast<unsigned>(std::min<int32_t>(count, 10));
	const unsigned samples = perShot * modeledShots;
	const Integer width = Integer(range.max) - range.min;
	const Integer base = Integer(range.min) * samples;
	// Native floors each shot's sample average separately. The floor remainder
	// bound guarantees S>=first; the upper raw-sum bound guarantees S<total.
	const Integer lower = first * perShot + (modeledShots - 1) * (perShot - 1) - base;
	const Integer upper = total * perShot - 1 - base;
	if(lower > upper) return 0;
	Integer outcomes = 1;
	for(unsigned index = 0; index < samples; ++index) outcomes *= width + 1;
	const Integer accepted = atMost(samples, width, upper, outcomes) - atMost(samples, width, lower - 1, outcomes);
	if(accepted <= 0) return 0;
	if(accepted >= outcomes) return 1;
	// Exact integer subtraction prevents tail cancellation. Divide at arbitrary
	// decimal precision BEFORE converting the bounded ratio to platform floating point.
	using Decimal = boost::multiprecision::cpp_dec_float_50;
	const Decimal ratio = Decimal(accepted) / Decimal(outcomes);
	const auto rounded = std::clamp(ratio.convert_to<long double>(), 0.0L, 1.0L);
	// A fractional tail must not round up into a stronger probability claim.
	return rounded > 0 ? std::nextafter(rounded, 0.0L) : 0;
}
}
