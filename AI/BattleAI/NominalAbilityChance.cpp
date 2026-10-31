/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NominalAbilityChance.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nominalAbilityChance
{
Outcomes harmful(const creatureAbilityChance::Rounding & chance, bool available)
{
	if(chance.remainderBasisPoints < 0 || chance.remainderBasisPoints >= 100
		|| (chance.remainderBasisPoints && (chance.wholePercent < 0 || chance.wholePercent >= 100)))
		throw std::invalid_argument("Invalid fractional ability chance");
	Outcomes result{};
	const auto add = [&](int rounded, double mass)
	{
		const double probability = std::clamp(rounded, 0, 100) / 100.0;
		if(!available)
		{
			result[2] += mass * (1 - probability);
			result[3] += mass * probability;
		}
		else if(rounded <= 0 || rounded >= 100)
		{
			result[0] += mass * (1 - probability);
			result[1] += mass * probability;
		}
		else
		{
			result[0] += mass * (1 - probability);
			result[2] += mass * probability * (1 - probability);
			result[3] += mass * probability * probability;
		}
	};
	const double fraction = chance.remainderBasisPoints / 100.0;
	add(chance.wholePercent, 1 - fraction);
	if(fraction > 0)
		add(chance.wholePercent + 1, fraction);
	return result;
}
namespace
{
void requireAvailability(double probability)
{
	if(!std::isfinite(probability) || probability < 0 || probability > 1)
		throw std::invalid_argument("Invalid nominal adverse-reroll availability");
}
}
double success(const creatureAbilityChance::Rounding & chance, double available)
{
	requireAvailability(available);
	const auto free = harmful(chance, true), spent = harmful(chance, false);
	return available * (free[1] + free[3]) + (1 - available) * spent[3];
}
double availableAfter(const creatureAbilityChance::Rounding & chance, double available)
{
	requireAvailability(available);
	const auto free = harmful(chance, true);
	return available * (free[0] + free[1]);
}
}
