/*
 * HeroCommandCoefficientMath.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommand.h"
#include "HeroCommandCoefficientMath.h"
#include <boost/multiprecision/cpp_int.hpp>

namespace heroCommands
{
namespace coefficientMath
{
namespace
{
int exactBoundedCoefficient(const std::array<double, 3> & terms, const std::array<int, 3> & factors)
{
	// Only the overflow fallback uses exact arithmetic. Keep the ordinary double
	// evaluation and its rounding unchanged, including for legacy saved rules.
	using boost::multiprecision::cpp_int;
	constexpr int digits = std::numeric_limits<double>::digits;
	static_assert(std::numeric_limits<double>::radix == 2 && digits <= 63);
	std::array<int64_t, 3> mantissas {};
	std::array<int, 3> exponents {};
	int lowestExponent = 0;
	for(size_t i = 0; i < terms.size(); ++i)
	{
		if(!std::isfinite(terms[i]))
			throw std::runtime_error("Invalid New Horizons command coefficient");
		if(terms[i] == 0 || factors[i] == 0)
			continue;
		const double fraction = std::frexp(terms[i], &exponents[i]);
		mantissas[i] = static_cast<int64_t>(std::ldexp(fraction, digits));
		exponents[i] -= digits;
		lowestExponent = std::min(lowestExponent, exponents[i]);
	}
	cpp_int sum = 0;
	for(size_t i = 0; i < terms.size(); ++i)
	{
		if(mantissas[i] == 0)
			continue;
		cpp_int part = mantissas[i];
		part *= factors[i];
		part <<= exponents[i] - lowestExponent;
		sum += part;
	}
	const bool negative = sum < 0;
	cpp_int magnitude = sum;
	if(negative)
		magnitude = -magnitude;
	if(lowestExponent < 0)
	{
		// Exact half-away-from-zero rounding, like lround, without passing NaN
		// or an unbounded result to a floating-to-integer conversion.
		cpp_int half = 1;
		half <<= -lowestExponent - 1;
		magnitude += half;
		magnitude >>= -lowestExponent;
	}
	cpp_int rounded = magnitude;
	if(negative)
		rounded = -rounded;
	// Integer endpoints make clamping before/after rounding equivalent.
	if(rounded < MIN_EFFECT_PERCENT)
		return MIN_EFFECT_PERCENT;
	if(rounded > MAX_EFFECT_PERCENT)
		return MAX_EFFECT_PERCENT;
	return rounded.convert_to<int>();
}
}

int boundedCoefficient(const std::array<double, 3> & terms, const std::array<int, 3> & factors)
{
	const double value = terms[0] * factors[0] + terms[1] * factors[1] + terms[2] * factors[2];
	if(!std::isfinite(value))
		return exactBoundedCoefficient(terms, factors);
	return static_cast<int>(std::lround(std::clamp(value,
		static_cast<double>(MIN_EFFECT_PERCENT), static_cast<double>(MAX_EFFECT_PERCENT))));
}

double boundedCoefficientComponent(double coefficient, double factor)
{
	if(!std::isfinite(coefficient) || !std::isfinite(factor))
		throw std::runtime_error("Invalid New Horizons command coefficient component");
	if(coefficient == 0 || factor == 0)
		return 0;
	const bool positive = std::signbit(coefficient) == std::signbit(factor);
	const double limit = positive ? MAX_EFFECT_PERCENT : -MIN_EFFECT_PERCENT;
	const double magnitudeFactor = std::abs(factor);
	if(std::abs(coefficient) > limit / magnitudeFactor)
		return positive ? MAX_EFFECT_PERCENT : MIN_EFFECT_PERCENT;
	return std::clamp(coefficient * factor,
		static_cast<double>(MIN_EFFECT_PERCENT), static_cast<double>(MAX_EFFECT_PERCENT));
}
}

int coefficient(const JsonNode & effect, int attack, int defense)
{
	return coefficientMath::boundedCoefficient({effect["base"].Float(), effect["attack"].Float(), effect["defense"].Float()},
		{1, attack, defense});
}
}
